#include "shared/i18n.hpp"
#include "shared/mux_export_helpers.hpp"

#include "main_window/preview_helpers.hpp"
#include "path_text.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <stop_token>
#include <utility>

namespace cristudio {
namespace {

QString ffmpeg_error_text(QProcess& process) {
    auto text = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (text.isEmpty()) {
        text = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    }
    return text;
}

enum class StagedStream {
    Video,
    Audio,
    Subtitle,
};

std::string staged_stream_error(StagedStream stream, bool flushing, const QString& detail) {
    const char* source = nullptr;
    if (flushing) {
        switch (stream) {
        case StagedStream::Video:
            source = "Could not flush the temporary mux video stream: %1";
            break;
        case StagedStream::Audio:
            source = "Could not flush the temporary mux audio stream: %1";
            break;
        case StagedStream::Subtitle:
            source = "Could not flush the temporary mux subtitle stream: %1";
            break;
        }
    } else {
        switch (stream) {
        case StagedStream::Video:
            source = "Could not write the temporary mux video stream: %1";
            break;
        case StagedStream::Audio:
            source = "Could not write the temporary mux audio stream: %1";
            break;
        case StagedStream::Subtitle:
            source = "Could not write the temporary mux subtitle stream: %1";
            break;
        }
    }
    return QCoreApplication::translate("Shared.MuxExportHelpers", source)
        .arg(detail)
        .toUtf8()
        .toStdString();
}

std::expected<void, std::string> write_staged_bytes(
    QFile& file,
    const char* bytes,
    size_t size,
    StagedStream stream,
    std::stop_token stop_token
) {
    constexpr size_t write_chunk_size = 4u * 1024u * 1024u;
    size_t written = 0;
    while (written < size) {
        if (stop_token.stop_requested()) {
            return std::unexpected(cristudio::i18n::translate_utf8("Shared.MuxExportHelpers", "extraction canceled"));
        }
        const auto chunk_size = static_cast<qint64>(std::min<size_t>(
            size - written,
            write_chunk_size
        ));
        const auto count = file.write(bytes + written, chunk_size);
        if (count <= 0) {
            return std::unexpected(staged_stream_error(stream, false, file.errorString()));
        }
        written += static_cast<size_t>(count);
    }
    if (!file.flush()) {
        return std::unexpected(staged_stream_error(stream, true, file.errorString()));
    }
    return {};
}

std::expected<void, std::string> write_staged_file(
    const QString& path,
    const char* bytes,
    size_t size,
    StagedStream stream,
    std::stop_token stop_token,
    QIODevice::OpenMode mode = QIODevice::WriteOnly
) {
    QFile file(path);
    if (!file.open(mode)) {
        return std::unexpected(staged_stream_error(stream, false, file.errorString()));
    }
    return write_staged_bytes(file, bytes, size, stream, stop_token);
}

std::expected<void, std::string> wait_for_process(
    QProcess& process,
    int timeout_ms,
    std::stop_token stop_token
) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (process.state() != QProcess::NotRunning) {
        if (stop_token.stop_requested()) {
            process.kill();
            process.waitForFinished(3000);
            return std::unexpected(cristudio::i18n::translate_utf8("Shared.MuxExportHelpers", "extraction canceled"));
        }
        const auto remaining = timeout_ms - static_cast<int>(elapsed.elapsed());
        if (remaining <= 0) {
            process.kill();
            process.waitForFinished(3000);
            return std::unexpected(cristudio::i18n::translate_utf8("Shared.MuxExportHelpers", "ffmpeg timed out"));
        }
        process.waitForFinished(std::min(remaining, 100));
    }
    return {};
}

std::expected<void, std::string> validate_mux_output_file(
    const std::filesystem::path& ffmpeg_path,
    const std::filesystem::path& output_path,
    std::stop_token stop_token
) {
    QProcess ffmpeg_process;
    ffmpeg_process.start(
        path_to_qstring(ffmpeg_path),
        QStringList{
            QStringLiteral("-hide_banner"),
            QStringLiteral("-loglevel"),
            QStringLiteral("error"),
            QStringLiteral("-xerror"),
            QStringLiteral("-y"),
            QStringLiteral("-i"),
            path_to_qstring(output_path),
            QStringLiteral("-map"),
            QStringLiteral("0:v:0"),
            QStringLiteral("-an"),
            QStringLiteral("-frames:v"),
            QStringLiteral("32"),
            QStringLiteral("-f"),
            QStringLiteral("null"),
            QStringLiteral("-"),
        }
    );
    const auto finished = wait_for_process(ffmpeg_process, 10000, stop_token);
    if (!finished) {
        return std::unexpected(finished.error());
    }
    const auto stderr_text = ffmpeg_error_text(ffmpeg_process);
    if (
        ffmpeg_process.exitStatus() == QProcess::NormalExit &&
        ffmpeg_process.exitCode() == 0 &&
        !has_video_decode_error(stderr_text)
    ) {
        return {};
    }
    return std::unexpected(cristudio::i18n::translate_utf8("Shared.MuxExportHelpers", "video may be encrypted or unsupported"));
}

} // namespace

std::expected<StagedMuxInputs, std::string> stage_mux_inputs(
    const MuxPreview& mux,
    const QString& directory,
    std::stop_token stop_token
) {
    StagedMuxInputs inputs;
    const QDir output_dir(directory);
    const auto video_suffix = mux.video_suffix.empty()
        ? QStringLiteral(".m2v")
        : utf8_to_qstring(mux.video_suffix);
    inputs.video_path = output_dir.filePath(QStringLiteral("video") + video_suffix);
    if (auto result = write_staged_file(
            inputs.video_path,
            reinterpret_cast<const char*>(mux.video_bytes.data()),
            mux.video_bytes.size(),
            StagedStream::Video,
            stop_token); !result) {
        return std::unexpected(result.error());
    }

    if (!mux.audio_wav_bytes.empty()) {
        inputs.audio_path = output_dir.filePath(QStringLiteral("audio.wav"));
        if (auto result = write_staged_file(
                inputs.audio_path,
                reinterpret_cast<const char*>(mux.audio_wav_bytes.data()),
                mux.audio_wav_bytes.size(),
                StagedStream::Audio,
                stop_token); !result) {
            return std::unexpected(result.error());
        }
    }

    inputs.subtitle_paths.reserve(static_cast<qsizetype>(mux.subtitle_choices.size()));
    for (size_t index = 0; index < mux.subtitle_choices.size(); ++index) {
        const auto& subtitle = mux.subtitle_choices[index];
        auto path = output_dir.filePath(QStringLiteral("subtitle-%1.srt").arg(index));
        if (auto result = write_staged_file(
                path,
                subtitle.srt_text.data(),
                subtitle.srt_text.size(),
                StagedStream::Subtitle,
                stop_token,
                QIODevice::WriteOnly | QIODevice::Text); !result) {
            return std::unexpected(result.error());
        }
        inputs.subtitle_paths.push_back(std::move(path));
    }
    return inputs;
}

QStringList mux_copy_arguments(const MuxPreview& mux, const StagedMuxInputs& inputs) {
    QStringList arguments{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-y"),
    };
    if (mux.frame_rate_n != 0 && mux.frame_rate_d != 0) {
        arguments << QStringLiteral("-r") << QStringLiteral("%1/%2").arg(mux.frame_rate_n).arg(mux.frame_rate_d);
    }
    if (!mux.ffmpeg_input_format.empty()) {
        arguments << QStringLiteral("-f") << utf8_to_qstring(mux.ffmpeg_input_format);
    }
    arguments << QStringLiteral("-i") << inputs.video_path;
    if (!inputs.audio_path.isEmpty()) {
        arguments << QStringLiteral("-i") << inputs.audio_path;
    }
    for (const auto& subtitle_path : inputs.subtitle_paths) {
        arguments << QStringLiteral("-i") << subtitle_path;
    }

    arguments << QStringLiteral("-map") << QStringLiteral("0:v:0");
    if (!inputs.audio_path.isEmpty()) {
        arguments << QStringLiteral("-map") << QStringLiteral("1:a:0");
    }
    const int subtitle_input_base = inputs.audio_path.isEmpty() ? 1 : 2;
    for (int index = 0; index < inputs.subtitle_paths.size(); ++index) {
        arguments << QStringLiteral("-map") << QStringLiteral("%1:0").arg(subtitle_input_base + index);
    }
    arguments << QStringLiteral("-c:v") << QStringLiteral("copy");
    if (!inputs.audio_path.isEmpty()) {
        arguments << QStringLiteral("-c:a") << QStringLiteral("copy");
    }
    if (!inputs.subtitle_paths.empty()) {
        arguments << QStringLiteral("-c:s") << QStringLiteral("srt");
        for (int index = 0; index < inputs.subtitle_paths.size(); ++index) {
            arguments
                << QStringLiteral("-metadata:s:s:%1").arg(index)
                << QStringLiteral("title=language %1").arg(
                    mux.subtitle_choices[static_cast<size_t>(index)].language_id);
        }
    }
    arguments
        << QStringLiteral("-max_interleave_delta") << QStringLiteral("0")
        << QStringLiteral("-muxdelay") << QStringLiteral("0")
        << QStringLiteral("-muxpreload") << QStringLiteral("0");
    return arguments;
}

std::expected<void, std::string> write_mux_extract_file(
    const MuxPreview& mux,
    const std::filesystem::path& output_path,
    const std::filesystem::path& ffmpeg_path,
    std::stop_token stop_token
) {
    if (stop_token.stop_requested()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.MuxExportHelpers", "extraction canceled"));
    }
    if (ffmpeg_path.empty()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.MuxExportHelpers", "ffmpeg not configured for mux extraction"));
    }
    if (mux.video_bytes.empty()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.MuxExportHelpers", "mux extraction did not produce video bytes"));
    }

    QTemporaryDir temp_dir(QDir::tempPath() + QStringLiteral("/CriStudio-mux-extract-XXXXXX"));
    if (!temp_dir.isValid()) {
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.MuxExportHelpers", "could not create temporary mux extraction directory"));
    }

    auto inputs = stage_mux_inputs(mux, temp_dir.path(), stop_token);
    if (!inputs) {
        return std::unexpected(inputs.error());
    }
    auto arguments = mux_copy_arguments(mux, *inputs);
    arguments
        << QStringLiteral("-f") << QStringLiteral("matroska")
        << path_to_qstring(output_path);

    QProcess ffmpeg_process;
    ffmpeg_process.start(path_to_qstring(ffmpeg_path), arguments);
    const auto waited = wait_for_process(ffmpeg_process, 30000, stop_token);
    const auto mux_ok = waited &&
        ffmpeg_process.exitStatus() == QProcess::NormalExit &&
        ffmpeg_process.exitCode() == 0 &&
        QFileInfo::exists(path_to_qstring(output_path));
    if (!mux_ok) {
        std::error_code ec;
        std::filesystem::remove(output_path, ec);
        if (!waited) {
            return std::unexpected(waited.error());
        }
        return std::unexpected(cristudio::i18n::translate_utf8("Shared.MuxExportHelpers", "ffmpeg stream-copy mux failed: ") + ffmpeg_error_text(ffmpeg_process).toStdString());
    }

    if (auto valid = validate_mux_output_file(ffmpeg_path, output_path, stop_token); !valid) {
        std::error_code ec;
        std::filesystem::remove(output_path, ec);
        return std::unexpected(valid.error());
    }
    return {};
}

} // namespace cristudio
