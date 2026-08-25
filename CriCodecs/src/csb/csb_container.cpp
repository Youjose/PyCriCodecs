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
    return io::write_file_bytes(output_path, m_source.bytes, "CSB save failed");
}

} // namespace cricodecs::csb
