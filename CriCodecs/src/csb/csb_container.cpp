/**
 * @file csb_container.cpp
 * @brief CSB container object helpers.
 *
 * The CSB object surface follows the vgmstream loader model for CRI UTF cue
 * archives and wrapper payloads. C++23 implementation and verification by
 * Youjose.
 */

#include "csb_container.hpp"

#include "../utilities/io.hpp"

namespace cricodecs::csb {

std::filesystem::path CsbStreamInfo::suggested_path() const {
    std::filesystem::path path =
        name.empty()
            ? std::filesystem::path("stream_" + std::to_string(row_index + 1))
            : std::filesystem::path(name);

    if (!path.has_extension()) {
        path += stream_file_extension(format);
    }

    return path;
}

std::expected<std::vector<uint8_t>, std::string> CsbContainer::save() const {
    return std::vector<uint8_t>(m_source.bytes.begin(), m_source.bytes.end());
}

std::expected<void, std::string> CsbContainer::save_to_file(const std::filesystem::path& output_path) const {
    std::error_code error;
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path(), error);
        if (error) {
            return std::unexpected(
                "CSB save failed: could not create output directory: " +
                output_path.parent_path().string());
        }
    }

    io::writer writer;
    if (auto result = writer.open(output_path); !result) {
        return std::unexpected("CSB save failed: could not open output file: " + output_path.string());
    }
    if (auto result = writer.write(m_source.bytes); !result) {
        return std::unexpected("CSB save failed: could not write output file: " + output_path.string());
    }
    if (auto result = writer.close(); !result) {
        return std::unexpected("CSB save failed: could not finalize output file: " + output_path.string());
    }

    return {};
}

} // namespace cricodecs::csb
