#pragma once
/**
 * @file cvm_builder.hpp
 * @brief CVM builder.
 *
 * Builds ROFS images with an embedded ISO9660 tree and optional TOC scrambling.
 */

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "cvm_build_script.hpp"

namespace cricodecs::cvm {

struct CvmBuildFile {
    std::filesystem::path archive_path;
    std::filesystem::path source_path;
    std::optional<std::vector<uint8_t>> data;
    std::optional<std::span<const uint8_t>> data_span;
};

struct CvmBuildDirectoryOptions {
    std::string disc_name;
    std::string recording_date;
    std::string media = "DVD";
    std::string system_identifier = "CRI ROFS";
    std::string volume_identifier;
    std::string volume_set_identifier;
    std::string publisher_identifier;
    std::string data_preparer_identifier;
    std::string application_identifier;
};

struct CvmBuildInput {
    std::string disc_name;
    std::string recording_date;
    std::string media = "DVD";
    std::string system_identifier = "CRI ROFS";
    std::string volume_identifier;
    std::string volume_set_identifier;
    std::string publisher_identifier;
    std::string data_preparer_identifier;
    std::string application_identifier;
    std::vector<CvmBuildFile> files;
    bool preserve_file_order = false;

    [[nodiscard]] static CvmBuildInput from_script(const CvmBuildScript& script);
    [[nodiscard]] static std::expected<CvmBuildInput, std::string> from_directory(
        const std::filesystem::path& input_dir,
        const CvmBuildDirectoryOptions& options = {}
    );
};

class CvmBuilder {
public:
    CvmBuilder() = default;

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build(
        const CvmBuildInput& input,
        std::string_view key = {}
    ) const;
    [[nodiscard]] std::expected<void, std::string> build_to_file(
        const std::filesystem::path& output_path,
        const CvmBuildInput& input,
        std::string_view key = {}
    ) const;

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build(
        const CvmBuildScript& script,
        std::string_view key = {}
    ) const;
    [[nodiscard]] std::expected<void, std::string> build_to_file(
        const std::filesystem::path& output_path,
        const CvmBuildScript& script,
        std::string_view key = {}
    ) const;
};

} // namespace cricodecs::cvm
