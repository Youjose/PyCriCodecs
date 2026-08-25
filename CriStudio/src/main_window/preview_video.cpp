#include "shared/i18n.hpp"
#include "../main_window.hpp"

#include "preview_helpers.hpp"
#include "ui_helpers.hpp"
#include "../path_text.hpp"
#include "../shared/document_preview_router.hpp"

#include <QCoreApplication>
#include <QAudioOutput>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QLabel>
#include <QMediaPlayer>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollArea>
#include <QSlider>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVideoWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrentRun>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <system_error>
#include <typeinfo>
#include <utility>

namespace cristudio {

void MainWindow::start_document_video_preview(const LoadedDocument& document) {
    if (preview_running() || !is_video_document(document)) {
        return;
    }

    m_current_preview_entry = std::nullopt;
    set_preview_entry_actions_visible(false);
    show_preview_document(document);

    const auto request_id = m_preview_request_id;
    show_media_preview_message(QCoreApplication::translate("MainWindow.PreviewVideo", "Loading video preview..."));
    append_log(QCoreApplication::translate("MainWindow.PreviewVideo", "Video preview started [%1]: %2")
        .arg(request_id)
        .arg(path_to_qstring(document.path)));

    m_preview_watcher->setFuture(QtConcurrent::run([document, request_id] {
        const auto stage = QCoreApplication::translate("MainWindow.PreviewVideo", "validating the video stream with ffmpeg");
        try {
            PreviewResult result;
            result.request_id = request_id;
            result.document = document;
            if (auto video = build_direct_video_preview(document)) {
                result.video = std::move(*video);
            } else {
                result.message = utf8_to_qstring(video.error());
            }
            return result;
        } catch (const std::exception& error) {
            PreviewResult result;
            result.request_id = request_id;
            result.document = document;
            result.message = QCoreApplication::translate("MainWindow.PreviewVideo", "Video preview failed while %1: %2 [%3]")
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
            result.message = QCoreApplication::translate("MainWindow.PreviewVideo", "Video preview failed while %1 with an unknown exception").arg(stage);
            return result;
        }
    }));
}

void prepare_video_preview_for_playback(VideoPreview& video) {
    if (!video.playable_path.empty() || video.video_bytes.empty()) {
        return;
    }

    QTemporaryDir temp_dir(QDir::tempPath() + QStringLiteral("/CriStudio-preview-XXXXXX"));
    if (!temp_dir.isValid()) {
        return;
    }

    const auto suffix = video.file_suffix.empty()
        ? QStringLiteral(".m2v")
        : utf8_to_qstring(video.file_suffix);
    const auto raw_path = temp_dir.filePath(QStringLiteral("preview") + suffix);
    QFile output(raw_path);
    if (!output.open(QIODevice::WriteOnly)) {
        return;
    }
    output.write(reinterpret_cast<const char*>(video.video_bytes.data()), static_cast<qsizetype>(video.video_bytes.size()));
    output.close();

    QString playback_path = raw_path;
    const auto ffmpeg = find_ffmpeg_executable();
    QString validation_error;
    if (
        video.remux_for_playback &&
        !video.ffmpeg_input_format.empty() &&
        video.frame_rate_n != 0 &&
        video.frame_rate_d != 0
    ) {
        if (!ffmpeg.isEmpty()) {
            const auto input_format = utf8_to_qstring(video.ffmpeg_input_format);
            const auto frame_rate = QStringLiteral("%1/%2").arg(video.frame_rate_n).arg(video.frame_rate_d);
            const auto output_suffix = input_format == QStringLiteral("h264")
                ? QStringLiteral(".mp4")
                : QStringLiteral(".ts");
            const auto remuxed_path = temp_dir.filePath(QStringLiteral("preview-remuxed") + output_suffix);
            QStringList arguments{
                QStringLiteral("-hide_banner"),
                QStringLiteral("-loglevel"),
                QStringLiteral("error"),
                QStringLiteral("-y"),
                QStringLiteral("-r"),
                frame_rate,
                QStringLiteral("-f"),
                input_format,
                QStringLiteral("-i"),
                raw_path,
                QStringLiteral("-map"),
                QStringLiteral("0:v:0"),
                QStringLiteral("-c:v"),
                QStringLiteral("copy"),
            };
            if (output_suffix == QStringLiteral(".mp4")) {
                arguments << QStringLiteral("-movflags") << QStringLiteral("+faststart");
            }
            arguments << remuxed_path;

            QProcess ffmpeg_process;
            ffmpeg_process.start(ffmpeg, arguments);
            if (ffmpeg_process.waitForFinished(30000) &&
                ffmpeg_process.exitStatus() == QProcess::NormalExit &&
                ffmpeg_process.exitCode() == 0 &&
                QFileInfo::exists(remuxed_path)) {
                playback_path = remuxed_path;
                video.format += cristudio::i18n::translate_utf8("MainWindow.PreviewVideo", " - timestamped preview");
            } else {
                validation_error = QString::fromLocal8Bit(ffmpeg_process.readAllStandardError()).trimmed();
            }
        } else {
            video.note = ffmpeg_missing_preview_message().toStdString();
            video.video_bytes.clear();
            video.video_bytes.shrink_to_fit();
            return;
        }
    }

    if (!ffmpeg.isEmpty() && !validate_video_preview_file(ffmpeg, playback_path, &validation_error)) {
        video.note = video_preview_unavailable_message().toStdString();
        video.video_bytes.clear();
        video.video_bytes.shrink_to_fit();
        return;
    }

    temp_dir.setAutoRemove(false);
    video.playable_path = path_from_qstring(playback_path);
    video.temporary_directory = path_from_qstring(temp_dir.path());
    video.video_bytes.clear();
    video.video_bytes.shrink_to_fit();
}



void MainWindow::configure_video_preview(const VideoPreview& video) {
    reset_audio_preview();
    m_video_temp_dir = video.temporary_directory;
    if (!ensure_media_backend()) {
        show_media_preview_message(QCoreApplication::translate("MainWindow.PreviewVideo", "Video preview backend is unavailable"));
        return;
    }
    auto playback_note = utf8_to_qstring(video.format);
    m_preview_duration_ms = static_cast<qint64>(std::min<uint64_t>(
        video.duration_ms,
        static_cast<uint64_t>(std::numeric_limits<qint64>::max())
    ));

    if (!video.playable_path.empty()) {
        m_audio_source_path = path_to_qstring(video.playable_path);
    } else if (!video.video_bytes.empty()) {
        m_audio_temp_dir = std::make_unique<QTemporaryDir>();
        if (!m_audio_temp_dir->isValid()) {
            m_media.status_label->setText(QCoreApplication::translate("MainWindow.PreviewVideo", "Could not create temporary video preview directory"));
            fade_widget_in(m_media.panel);
            return;
        }

        const auto suffix = video.file_suffix.empty()
            ? QStringLiteral(".m2v")
            : utf8_to_qstring(video.file_suffix);
        const auto output_path = m_audio_temp_dir->filePath(QStringLiteral("preview") + suffix);
        QFile output(output_path);
        if (!output.open(QIODevice::WriteOnly)) {
            m_media.status_label->setText(QCoreApplication::translate("MainWindow.PreviewVideo", "Could not write temporary video preview"));
            fade_widget_in(m_media.panel);
            return;
        }
        output.write(reinterpret_cast<const char*>(video.video_bytes.data()), static_cast<qsizetype>(video.video_bytes.size()));
        output.close();
        m_audio_source_path = output_path;
        if (video.remux_for_playback) {
            playback_note += QCoreApplication::translate("MainWindow.PreviewVideo", " - raw stream preview");
        }
    }

    if (m_audio_source_path.isEmpty()) {
        m_media.status_label->setText(video.note.empty()
            ? QCoreApplication::translate("MainWindow.PreviewVideo", "Video preview is unavailable")
            : utf8_to_qstring(video.note));
        fade_widget_in(m_media.panel);
        return;
    }

    m_audio_player->setVideoOutput(m_video.widget);
    m_audio_player->setSource(QUrl::fromLocalFile(m_audio_source_path));
    if (m_preview_duration_ms > 0) {
        m_media.seek_slider->setRange(0, static_cast<int>(std::clamp<qint64>(
            m_preview_duration_ms,
            0,
            std::numeric_limits<int>::max()
        )));
    }
    m_media.status_label->setText(playback_note);
    m_video.frame->show();
    m_video.widget->show();
    show_playable_media_controls();
    m_nested_entry_view->hide();
    m_nested_image_scroll->hide();
    m_nested_body->hide();
}

void MainWindow::release_video_preview_resources() {
    if (m_video_temp_dir.empty()) {
        return;
    }

    auto temp_dir = std::move(m_video_temp_dir);
    m_video_temp_dir.clear();
    m_deferred_video_temp_dirs.push_back(temp_dir);
    QTimer::singleShot(2000, this, [this, temp_dir = std::move(temp_dir)] {
        std::error_code remove_error;
        std::filesystem::remove_all(temp_dir, remove_error);
        std::erase(m_deferred_video_temp_dirs, temp_dir);
    });
}

void MainWindow::recreate_video_widget() {
    auto* old_widget = m_video.widget;
    auto* box = qobject_cast<QVBoxLayout*>(m_video.frame->layout());
    const auto index = box != nullptr ? box->indexOf(old_widget) : -1;

    old_widget->hide();
    if (box != nullptr) {
        box->removeWidget(old_widget);
    }
    old_widget->deleteLater();

    m_video.widget = new QVideoWidget(m_video.frame);
    m_video.widget->setMinimumHeight(260);
    m_video.widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_video.widget->setAspectRatioMode(Qt::KeepAspectRatio);
    m_video.widget->hide();
    if (box != nullptr && index >= 0) {
        box->insertWidget(index, m_video.widget);
    }
}

void MainWindow::set_preview_image(const QImage& image) {
    m_nested_source_pixmap = QPixmap::fromImage(image);
    update_preview_image();
}

void MainWindow::update_preview_image() {
    if (m_nested_image_scroll == nullptr || m_nested_image == nullptr || m_nested_source_pixmap.isNull()) {
        return;
    }

    const auto viewport_size = m_nested_image_scroll->viewport() != nullptr
        ? m_nested_image_scroll->viewport()->size()
        : m_nested_image_scroll->size();
    if (viewport_size.isEmpty()) {
        return;
    }

    auto shown = m_nested_source_pixmap;
    if (shown.width() > viewport_size.width() || shown.height() > viewport_size.height()) {
        shown = m_nested_source_pixmap.scaled(
            viewport_size,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        );
    }

    m_nested_image->setPixmap(shown);
    m_nested_image->setMinimumSize(shown.size());
    m_nested_image->setAlignment(Qt::AlignCenter);
}



} // namespace cristudio
