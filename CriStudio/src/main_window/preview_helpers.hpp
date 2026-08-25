#pragma once

#include "../document_loader.hpp"

#include <QString>

class QProcess;

namespace cristudio {

bool is_audio_document(const LoadedDocument& document);
bool is_direct_audio_document(const LoadedDocument& document);
bool is_video_document(const LoadedDocument& document);
bool is_mux_document(const LoadedDocument& document);
bool is_low_signal_loader_message(const QString& message);
QString video_preview_unavailable_message();
QString find_ffmpeg_executable();
QString ffmpeg_missing_preview_message();
bool has_video_decode_error(const QString& stderr_text);
bool validate_video_preview_file(const QString& ffmpeg, const QString& path, QString* error = nullptr);
QString time_text(qint64 milliseconds);
void prepare_video_preview_for_playback(VideoPreview& video);
void remove_preview_temporary_directory(VideoPreview& video);
void remove_preview_temporary_directory(MuxPreview& mux);

} // namespace cristudio
