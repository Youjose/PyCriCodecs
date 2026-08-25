#pragma once
/**
 * @file cvm_path.hpp
 * @brief CVM/ROFS archive-path normalization helpers.
 */

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "../utilities/string.hpp"

namespace cricodecs::cvm {

[[nodiscard]] inline std::string normalize_archive_path(const std::filesystem::path& path) {
    std::filesystem::path normalized_path = path.lexically_normal();
    std::string normalized = normalized_path.generic_string();
    normalized.erase(0, normalized.find_first_not_of('/'));
    if (normalized == ".") {
        return {};
    }
    return normalized;
}

[[nodiscard]] inline std::string normalize_archive_lookup_key(const std::filesystem::path& path) {
    return util::uppercase_ascii(normalize_archive_path(path));
}

[[nodiscard]] inline bool is_root_archive_path(const std::filesystem::path& path) {
    return normalize_archive_path(path).empty();
}

[[nodiscard]] inline std::string default_disc_name(
    const std::filesystem::path& source_path,
    std::string_view volume_identifier
) {
    if (!source_path.empty() && source_path.has_filename()) {
        return source_path.filename().generic_string();
    }
    return volume_identifier.empty() ? "image.cvm" : std::string(volume_identifier) + ".cvm";
}

[[nodiscard]] inline std::filesystem::path resolve_directory_relative_path(
    const std::filesystem::path& current_directory,
    const std::filesystem::path& requested_path
) {
    const std::string raw = requested_path.generic_string();
    const bool absolute_from_root = !raw.empty() && raw.front() == '/';

    std::vector<std::string> components;
    if (!absolute_from_root) {
        for (const auto& part : current_directory) {
            const std::string text = part.generic_string();
            if (!text.empty() && text != ".") {
                components.push_back(text);
            }
        }
    }

    for (const auto& part : requested_path) {
        const std::string text = part.generic_string();
        if (text.empty() || text == ".") {
            continue;
        }
        if (text == "..") {
            if (!components.empty()) {
                components.pop_back();
            }
            continue;
        }
        components.push_back(text);
    }

    std::filesystem::path resolved;
    for (const auto& component : components) {
        resolved /= component;
    }
    return std::filesystem::path(normalize_archive_path(resolved));
}

} // namespace cricodecs::cvm
