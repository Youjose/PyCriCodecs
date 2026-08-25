#include "shared/i18n.hpp"
#include "../main_window.hpp"

#include "preview_mux.hpp"

#include "preview_helpers.hpp"
#include "ui_helpers.hpp"
#include "../path_text.hpp"
#include "shared/mux_export_helpers.hpp"

#include <QCoreApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QMediaPlayer>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVideoWidget>
#include <QWidget>
#include <QtConcurrentRun>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <typeinfo>
#include <utility>

namespace cristudio {

void prepare_mux_preview_for_playback(MuxPreview& mux) {
    if (!mux.playable_path.empty() || mux.video_bytes.empty()) {
        return;
    }

    QTemporaryDir temp_dir(QDir::tempPath() + QStringLiteral("/CriStudio-mux-preview-XXXXXX"));
    if (!temp_dir.isValid()) {
        mux.note = cristudio::i18n::translate_utf8("MainWindow.PreviewMux", "could not create temporary mux preview directory");
        return;
    }

    const auto ffmpeg = find_ffmpeg_executable();
    if (ffmpeg.isEmpty()) {
        mux.note = ffmpeg_missing_preview_message().toStdString();
        return;
    }

    auto inputs = stage_mux_inputs(mux, temp_dir.path());
    if (!inputs) {
        mux.note = std::move(inputs.error());
        return;
    }
    QString mux_error;
    QString playable_path;
    const auto validate_mux_preview = [&](QString const& output_path) {
        if (validate_video_preview_file(ffmpeg, output_path, &mux_error)) {
            return true;
        }
        QFile::remove(output_path);
        return false;
    };

    const auto run_copy_remux = [&](QString const& output_path) {
        auto arguments = mux_copy_arguments(mux, *inputs);
        if (output_path.endsWith(QStringLiteral(".mov")) || output_path.endsWith(QStringLiteral(".mp4"))) {
            arguments << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        }
        arguments << output_path;

        QProcess ffmpeg_process;
        ffmpeg_process.start(ffmpeg, arguments);
        const auto remux_ok =
            ffmpeg_process.waitForFinished(30000) &&
            ffmpeg_process.exitStatus() == QProcess::NormalExit &&
            ffmpeg_process.exitCode() == 0 &&
            QFileInfo::exists(output_path);
        if (remux_ok && validate_mux_preview(output_path)) {
            playable_path = output_path;
            return true;
        }

        if (!remux_ok) {
            mux_error = QString::fromLocal8Bit(ffmpeg_process.readAllStandardError()).trimmed();
        }
        return false;
    };

    if (playable_path.isEmpty()) {
        run_copy_remux(temp_dir.filePath(QStringLiteral("mux-preview.mkv")));
    }
    if (playable_path.isEmpty() && inputs->subtitle_paths.empty()) {
        run_copy_remux(temp_dir.filePath(QStringLiteral("mux-preview.mov")));
    }
    if (playable_path.isEmpty() && inputs->subtitle_paths.empty()) {
        run_copy_remux(temp_dir.filePath(QStringLiteral("mux-preview.ts")));
    }

    if (playable_path.isEmpty()) {
        mux.note = video_preview_unavailable_message().toStdString();
        return;
    }

    temp_dir.setAutoRemove(false);
    mux.playable_path = path_from_qstring(playable_path);
    mux.temporary_directory = path_from_qstring(temp_dir.path());
    mux.video_bytes.clear();
    mux.video_bytes.shrink_to_fit();
    mux.audio_wav_bytes.clear();
    mux.audio_wav_bytes.shrink_to_fit();
}
void MainWindow::start_document_mux_preview(const LoadedDocument& document, int audio_choice) {
    if (preview_running()) {
        ++m_preview_request_id;
        m_pending_preview_entry = std::nullopt;
        m_pending_mux_preview = std::pair{document.path, audio_choice};
        show_preview_document(document);
        show_media_preview_message(QCoreApplication::translate("MainWindow.PreviewMux", "Loading mux preview..."));
        return;
    }

    m_pending_mux_preview = std::nullopt;
    m_current_preview_entry = std::nullopt;
    set_preview_entry_actions_visible(false);
    open_preview_panel();
    m_nested_title->setText(archive_basename(utf8_to_qstring(document.display_name)));
    m_nested_subtitle->setText(QCoreApplication::translate("MainWindow.PreviewMux", "Mux preview"));
    populate_info_grid(m_nested_info_grid, document.info);
    update_preview_key_panel(&document);
    reset_audio_preview();
    m_nested_entry_model->clear();
    m_nested_entry_view->hide();
    m_nested_image_scroll->hide();

    if (!has_ffmpeg()) {
        show_media_preview_message(ffmpeg_missing_message());
        return;
    }

    show_media_preview_message(QCoreApplication::translate("MainWindow.PreviewMux", "Loading mux preview..."));
    if (m_preview_tabs != nullptr) {
        m_preview_tabs->show();
        m_preview_tabs->setCurrentIndex(0);
    }

    const auto request_id = ++m_preview_request_id;
    append_log(QCoreApplication::translate("MainWindow.PreviewMux", "Mux preview started [%1, audio %2]: %3")
        .arg(request_id)
        .arg(audio_choice)
        .arg(path_to_qstring(document.path)));
    auto keys = m_decryption_keys;
    m_preview_watcher->setFuture(QtConcurrent::run([document, request_id, audio_choice, keys = std::move(keys)] {
        auto stage = QCoreApplication::translate("MainWindow.PreviewMux", "extracting and decoding USM/SFD streams");
        try {
            const auto make_result = [request_id, &document, audio_choice, &stage](const DecryptionKeys& preview_keys) {
                PreviewResult result;
                result.request_id = request_id;
                result.document = document;
                stage = QCoreApplication::translate("MainWindow.PreviewMux", "extracting and decoding USM/SFD streams");
                auto mux = build_mux_preview(document, audio_choice, preview_keys);
                if (!mux) {
                    result.message = QString::fromStdString(mux.error());
                    return result;
                }
                stage = QCoreApplication::translate("MainWindow.PreviewMux", "writing temporary streams and running FFmpeg remux");
                prepare_mux_preview_for_playback(*mux);
                if (mux->playable_path.empty() && !mux->note.empty()) {
                    result.message = QString::fromStdString(mux->note);
                }
                result.mux = std::move(*mux);
                return result;
            };

            auto result = make_result(keys);
            if (keys.has_cri_key && (!result.mux || result.mux->playable_path.empty())) {
                auto fallback_keys = keys;
                fallback_keys.has_cri_key = false;
                fallback_keys.cri_key = 0;
                auto fallback_result = make_result(fallback_keys);
                if (fallback_result.mux && !fallback_result.mux->playable_path.empty()) {
                    if (result.mux) {
                        remove_preview_temporary_directory(*result.mux);
                    }
                    result = std::move(fallback_result);
                } else if (fallback_result.mux) {
                    remove_preview_temporary_directory(*fallback_result.mux);
                }
            }
            return result;
        } catch (const std::exception& error) {
            PreviewResult result;
            result.request_id = request_id;
            result.document = document;
            result.message = QCoreApplication::translate("MainWindow.PreviewMux", "Mux preview failed while %1: %2 [%3]")
                .arg(
                    stage,
                    QString::fromLocal8Bit(error.what()),
                    QString::fromLatin1(typeid(error).name())
                );
            return result;
        } catch (...) {
            PreviewResult result;
            result.request_id = request_id;
            result.document = document;
            result.message = QCoreApplication::translate("MainWindow.PreviewMux", "Mux preview failed while %1 with an unknown exception").arg(stage);
            return result;
        }
    }));
}

void MainWindow::configure_mux_preview(const MuxPreview& mux) {
    reset_audio_preview();
    m_video_temp_dir = mux.temporary_directory;
    if (!ensure_media_backend()) {
        show_media_preview_message(QCoreApplication::translate("MainWindow.PreviewMux", "Mux preview backend is unavailable"));
        return;
    }
    {
        const QSignalBlocker blocker(m_media.audio_combo);
        m_media.audio_combo->clear();
        for (int index = 0; index < static_cast<int>(mux.audio_choices.size()); ++index) {
            const auto& choice = mux.audio_choices[static_cast<size_t>(index)];
            auto label = archive_basename(strip_mux_prefix(utf8_to_qstring(choice.name)));
            if (!choice.detail.empty()) {
                label += QStringLiteral("  -  ") + utf8_to_qstring(choice.detail);
            }
            m_media.audio_combo->addItem(label, index);
        }
        if (mux.selected_audio >= 0 && mux.selected_audio < m_media.audio_combo->count()) {
            m_media.audio_combo->setCurrentIndex(mux.selected_audio);
        }
    }
    m_media.audio_row->setVisible(m_media.audio_combo->count() > 0);
    {
        const QSignalBlocker blocker(m_media.subtitle_combo);
        m_media.subtitle_combo->clear();
        m_media.subtitle_combo->addItem(QCoreApplication::translate("MainWindow.PreviewMux", "Disabled"), -1);
        for (int i = 0; i < static_cast<int>(mux.subtitle_choices.size()); ++i) {
            const auto& choice = mux.subtitle_choices[static_cast<size_t>(i)];
            auto label = utf8_to_qstring(choice.detail.empty() ? choice.name : choice.detail);
            if (!choice.name.empty()) {
                label += QStringLiteral("  -  ") + archive_basename(strip_mux_prefix(utf8_to_qstring(choice.name)));
            }
            m_media.subtitle_combo->addItem(label, i);
        }
        if (mux.selected_subtitle >= 0 && mux.selected_subtitle + 1 < m_media.subtitle_combo->count()) {
            m_media.subtitle_combo->setCurrentIndex(mux.selected_subtitle + 1);
        } else {
            m_media.subtitle_combo->setCurrentIndex(0);
        }
    }
    m_media.subtitle_row->setVisible(!mux.subtitle_choices.empty());

    if (mux.playable_path.empty()) {
        show_media_preview_message(mux.note.empty()
            ? QCoreApplication::translate("MainWindow.PreviewMux", "Mux preview is unavailable")
            : utf8_to_qstring(mux.note));
        return;
    }

    m_audio_source_path = path_to_qstring(mux.playable_path);
    m_preview_duration_ms = static_cast<qint64>(std::min<uint64_t>(
        mux.duration_ms,
        static_cast<uint64_t>(std::numeric_limits<qint64>::max())
    ));
    if (m_preview_duration_ms > 0) {
        m_media.seek_slider->setRange(0, static_cast<int>(std::clamp<qint64>(
            m_preview_duration_ms,
            0,
            std::numeric_limits<int>::max()
        )));
    }

    m_audio_player->setVideoOutput(m_video.widget);
    m_audio_player->setSource(QUrl::fromLocalFile(m_audio_source_path));
    if (m_media.subtitle_combo->currentIndex() >= 0) {
        m_audio_player->setActiveSubtitleTrack(m_media.subtitle_combo->currentData().toInt());
    }
    auto label = QCoreApplication::translate("MainWindow.PreviewMux", "Mux preview - ") + utf8_to_qstring(mux.format);
    if (!mux.audio_label.empty()) {
        label += QStringLiteral(" + ") + archive_basename(strip_mux_prefix(utf8_to_qstring(mux.audio_label)));
    } else {
        label += QCoreApplication::translate("MainWindow.PreviewMux", " (video only)");
    }
    m_media.status_label->setText(label);
    m_video.frame->show();
    m_video.widget->show();
    show_playable_media_controls();
    m_nested_entry_view->hide();
    m_nested_image_scroll->hide();
    m_nested_body->hide();
}



} // namespace cristudio
