#include "../main_window.hpp"

#include "preview_helpers.hpp"
#include "ui_helpers.hpp"
#include "../path_text.hpp"

#include <QCoreApplication>
#include <QAudioOutput>
#include <QCheckBox>
#include <QComboBox>
#include <QFutureWatcher>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMediaPlayer>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVideoWidget>
#include <QWidget>
#include <QtConcurrentRun>

#include <algorithm>
#include <exception>
#include <limits>
#include <typeinfo>
#include <utility>

namespace cristudio {

void MainWindow::start_document_audio_preview(const LoadedDocument& document) {
    if (preview_running()) {
        return;
    }

    m_current_preview_entry = std::nullopt;
    set_preview_entry_actions_visible(false);
    show_preview_document(document);
    if (!is_direct_audio_document(document)) {
        return;
    }

    const auto request_id = m_preview_request_id;
    show_media_preview_message(QCoreApplication::translate("MainWindow.PreviewAudio", "Loading audio preview..."));
    append_log(QCoreApplication::translate("MainWindow.PreviewAudio", "Audio preview started [%1]: %2")
        .arg(request_id)
        .arg(path_to_qstring(document.path)));

    auto keys = m_decryption_keys;
    m_preview_watcher->setFuture(QtConcurrent::run([document, request_id, keys = std::move(keys)] {
        const auto stage = QCoreApplication::translate("MainWindow.PreviewAudio", "extracting and decoding the audio stream");
        try {
            PreviewResult result;
            result.request_id = request_id;
            result.document = document;
            if (auto audio = build_audio_preview(document, keys); audio) {
                result.audio = std::move(*audio);
            } else {
                result.message = utf8_to_qstring(audio.error());
            }
            return result;
        } catch (const std::exception& error) {
            PreviewResult result;
            result.request_id = request_id;
            result.document = document;
            result.message = QCoreApplication::translate("MainWindow.PreviewAudio", "Audio preview failed while %1: %2 [%3]")
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
            result.message = QCoreApplication::translate("MainWindow.PreviewAudio", "Audio preview failed while %1 with an unknown exception").arg(stage);
            return result;
        }
    }));
}

void MainWindow::configure_audio_preview(const AudioPreview& audio) {
    reset_audio_preview();
    if (!ensure_media_backend()) {
        show_media_preview_message(QCoreApplication::translate("MainWindow.PreviewAudio", "Audio preview backend is unavailable"));
        return;
    }
    m_video.widget->hide();
    m_video.frame->hide();
    m_audio_sample_rate = audio.sample_rate;
    m_audio_loops = audio.loops;

    if (!audio.playable_path.empty()) {
        m_audio_source_path = path_to_qstring(audio.playable_path);
    } else if (!audio.wav_bytes.empty()) {
        m_audio_temp_dir = std::make_unique<QTemporaryDir>();
        if (!m_audio_temp_dir->isValid()) {
            m_media.status_label->setText(QCoreApplication::translate("MainWindow.PreviewAudio", "Could not create temporary playback directory"));
            fade_widget_in(m_media.panel);
            return;
        }

        const auto output_path = m_audio_temp_dir->filePath(QStringLiteral("preview.wav"));
        QFile output(output_path);
        if (!output.open(QIODevice::WriteOnly)) {
            m_media.status_label->setText(QCoreApplication::translate("MainWindow.PreviewAudio", "Could not write temporary WAV preview"));
            fade_widget_in(m_media.panel);
            return;
        }
        output.write(reinterpret_cast<const char*>(audio.wav_bytes.data()), static_cast<qsizetype>(audio.wav_bytes.size()));
        output.close();
        m_audio_source_path = output_path;
    }

    if (m_audio_source_path.isEmpty()) {
        m_media.status_label->setText(QCoreApplication::translate("MainWindow.PreviewAudio", "Audio preview is unavailable"));
        fade_widget_in(m_media.panel);
        return;
    }

    const auto duration_ms = audio.sample_rate == 0
        ? 0
        : static_cast<qint64>((audio.sample_count * 1000ull) / audio.sample_rate);
    m_preview_duration_ms = duration_ms;
    m_media.seek_slider->setRange(0, static_cast<int>(std::clamp<qint64>(duration_ms, 0, std::numeric_limits<int>::max())));
    m_media.seek_slider->setValue(0);
    m_audio_player->setSource(QUrl::fromLocalFile(m_audio_source_path));
    m_media.status_label->setText(QCoreApplication::translate("MainWindow.PreviewAudio", "%1 - %2 ch, %3 Hz")
        .arg(utf8_to_qstring(audio.format))
        .arg(audio.channels)
        .arg(audio.sample_rate));
    update_loop_controls(audio);
    show_playable_media_controls();
    m_nested_body->hide();
}

void MainWindow::reset_audio_preview() {
    const bool had_video_preview = m_audio_player != nullptr && m_audio_player->videoOutput() != nullptr;
    if (m_audio_player != nullptr) {
        m_audio_player->setVideoOutput(nullptr);
        m_audio_player->stop();
        m_audio_player->setSource({});
    }
    m_video.widget->hide();
    m_video.widget->repaint();
    m_video.frame->hide();
    m_video.frame->repaint();
    m_audio_source_path.clear();
    m_audio_temp_dir.reset();
    release_video_preview_resources();
    m_audio_sample_rate = 0;
    m_audio_loops.clear();
    m_preview_duration_ms = 0;
    m_audio_slider_dragging = false;
    m_audio_resume_after_seek = false;
    m_audio_loop_seeking = false;
    m_media.play_button->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_media.play_button->setText(QCoreApplication::translate("MainWindow.PreviewAudio", "Play"));
    m_media.play_button->setEnabled(false);
    m_media.seek_slider->setRange(0, 0);
    m_media.seek_slider->setValue(0);
    m_media.seek_slider->setEnabled(false);
    m_media.time_label->setText(QStringLiteral("0:00 / 0:00"));
    m_media.volume_label->hide();
    m_media.volume_slider->hide();
    m_media.volume_slider->setEnabled(false);
    m_media.status_label->setText(QCoreApplication::translate("MainWindow.PreviewAudio", "No playable audio selected"));
    const QSignalBlocker loop_toggle_blocker(m_media.loop_toggle);
    const QSignalBlocker loop_list_blocker(m_media.loop_list);
    const QSignalBlocker audio_blocker(m_media.audio_combo);
    const QSignalBlocker subtitle_blocker(m_media.subtitle_combo);
    m_media.loop_toggle->setChecked(false);
    m_media.loop_toggle->setEnabled(false);
    m_media.loop_list->clear();
    m_media.loop_list->setEnabled(false);
    m_media.loop_row->hide();
    m_media.audio_combo->clear();
    m_media.audio_row->hide();
    m_media.subtitle_combo->clear();
    m_media.subtitle_row->hide();
    m_media.panel->hide();
    if (had_video_preview) {
        recreate_video_widget();
    }
}

void MainWindow::update_audio_time_label() {
    if (m_audio_player == nullptr) {
        return;
    }

    auto duration = m_audio_player->duration();
    if (m_preview_duration_ms > 0) {
        duration = m_preview_duration_ms;
    }
    const auto position = m_audio_slider_dragging
        ? static_cast<qint64>(m_media.seek_slider->value())
        : m_audio_player->position();
    m_media.time_label->setText(time_text(position) + QStringLiteral(" / ") + time_text(duration));
}

void MainWindow::update_loop_controls(const AudioPreview& audio) {
    QSignalBlocker toggle_blocker(m_media.loop_toggle);
    QSignalBlocker list_blocker(m_media.loop_list);
    m_media.loop_list->clear();

    const auto sample_rate = audio.sample_rate;
    const auto to_ms = [sample_rate](uint64_t sample) -> qint64 {
        if (sample_rate == 0) {
            return 0;
        }
        return static_cast<qint64>((sample * 1000ull) / sample_rate);
    };

    for (size_t i = 0; i < m_audio_loops.size(); ++i) {
        const auto& loop = m_audio_loops[i];
        auto name = utf8_to_qstring(loop.name);
        if (const auto bracket = name.indexOf(QStringLiteral(" [")); bracket > 0) {
            name = name.left(bracket);
        }
        const auto label = QCoreApplication::translate("MainWindow.PreviewAudio", "%1    %2 - %3    samples %4 - %5")
            .arg(name)
            .arg(time_text(to_ms(loop.start_sample)))
            .arg(time_text(to_ms(loop.end_sample)))
            .arg(loop.start_sample)
            .arg(loop.end_sample);
        auto* item = new QListWidgetItem(label, m_media.loop_list);
        item->setData(Qt::UserRole, static_cast<int>(i));
        item->setSizeHint(QSize(0, 30));
        item->setToolTip(QCoreApplication::translate("MainWindow.PreviewAudio", "%1: samples %2 - %3, time %4 - %5")
            .arg(name)
            .arg(loop.start_sample)
            .arg(loop.end_sample)
            .arg(time_text(to_ms(loop.start_sample)))
            .arg(time_text(to_ms(loop.end_sample))));
    }

    const auto has_loops = !m_audio_loops.empty() && sample_rate != 0;
    m_media.loop_row->setVisible(has_loops);
    m_media.loop_toggle->setEnabled(has_loops);
    m_media.loop_toggle->setChecked(false);
    m_media.loop_list->setEnabled(has_loops);
    if (has_loops) {
        const auto visible_rows = std::min<int>(4, static_cast<int>(m_audio_loops.size()));
        m_media.loop_list->setFixedHeight(std::max(36, 32 * visible_rows + 6));
        m_media.loop_list->setCurrentRow(0);
    }
}

void MainWindow::handle_loop_position(qint64 position) {
    if (
        m_audio_player == nullptr ||
        !m_media.loop_toggle->isChecked() ||
        m_audio_loop_seeking ||
        m_audio_slider_dragging ||
        m_audio_sample_rate == 0
    ) {
        return;
    }

    const auto loop_index = m_media.loop_list->currentRow();
    if (loop_index < 0 || loop_index >= static_cast<int>(m_audio_loops.size())) {
        return;
    }

    const auto& loop = m_audio_loops[static_cast<size_t>(loop_index)];
    const auto start_ms = static_cast<qint64>((loop.start_sample * 1000ull) / m_audio_sample_rate);
    const auto end_ms = static_cast<qint64>((loop.end_sample * 1000ull) / m_audio_sample_rate);
    if (end_ms <= start_ms || position < end_ms) {
        return;
    }

    m_audio_loop_seeking = true;
    m_audio_player->setPosition(start_ms);
    if (m_audio_player->playbackState() != QMediaPlayer::PlayingState && !m_audio_source_path.isEmpty()) {
        m_audio_player->play();
    }
    m_audio_loop_seeking = false;
}



} // namespace cristudio
