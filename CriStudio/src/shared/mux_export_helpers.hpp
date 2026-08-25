#pragma once

#include "document/document_types.hpp"

#include <expected>
#include <filesystem>
#include <stop_token>
#include <string>

#include <QString>
#include <QStringList>

namespace cristudio {

struct StagedMuxInputs {
    QString video_path;
    QString audio_path;
    QStringList subtitle_paths;
};

[[nodiscard]] std::expected<StagedMuxInputs, std::string> stage_mux_inputs(
    const MuxPreview& mux,
    const QString& directory,
    std::stop_token stop_token = {}
);

[[nodiscard]] QStringList mux_copy_arguments(
    const MuxPreview& mux,
    const StagedMuxInputs& inputs
);

[[nodiscard]] std::expected<void, std::string> write_mux_extract_file(
    const MuxPreview& mux,
    const std::filesystem::path& output_path,
    const std::filesystem::path& ffmpeg_path,
    std::stop_token stop_token = {}
);

} // namespace cristudio
