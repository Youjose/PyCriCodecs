#pragma once

#include "cpk_container.hpp"
#include "document/document_types.hpp"
#include "modules/transform_detail.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cristudio::modules::cpk {

struct ScratchArchive {
    cricodecs::cpk::Cpk container;
    LoadedDocument document;
};

[[nodiscard]] ScratchArchive create_scratch_archive(
    cricodecs::cpk::CpkPreset preset = cricodecs::cpk::CpkPreset::Filename
);

[[nodiscard]] std::vector<TransformDetailRow> detail_rows(const cricodecs::cpk::Cpk& cpk);

void add_bytes(
    cricodecs::cpk::Cpk& cpk,
    std::span<const uint8_t> bytes,
    const std::string& archive_path
);

struct AddFileSource {
    std::filesystem::path local_path;
    std::string archive_path;
};

[[nodiscard]] std::expected<size_t, std::string> add_files(
    cricodecs::cpk::Cpk& cpk,
    std::span<const AddFileSource> files
);

[[nodiscard]] std::expected<size_t, std::string> add_directory(
    cricodecs::cpk::Cpk& cpk,
    const std::filesystem::path& root
);

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build_session_bytes(
    cricodecs::cpk::Cpk& cpk,
    bool obfuscate_utf = false
);

void set_options(
    cricodecs::cpk::Cpk& cpk,
    cricodecs::cpk::CpkOptions options
);

[[nodiscard]] std::expected<void, std::string> replace_bytes(
    cricodecs::cpk::Cpk& cpk,
    size_t index,
    std::span<const uint8_t> bytes
);

[[nodiscard]] std::expected<void, std::string> remove_file(
    cricodecs::cpk::Cpk& cpk,
    size_t index
);

[[nodiscard]] std::expected<void, std::string> move_file(
    cricodecs::cpk::Cpk& cpk,
    size_t from_index,
    size_t to_index
);

[[nodiscard]] std::expected<void, std::string> rename_file(
    cricodecs::cpk::Cpk& cpk,
    size_t index,
    const std::string& archive_path
);

[[nodiscard]] std::expected<void, std::string> set_dirname(
    cricodecs::cpk::Cpk& cpk,
    size_t index,
    const std::string& dirname
);

[[nodiscard]] std::expected<void, std::string> set_filename(
    cricodecs::cpk::Cpk& cpk,
    size_t index,
    const std::string& filename
);

[[nodiscard]] std::expected<void, std::string> set_entry_id(
    cricodecs::cpk::Cpk& cpk,
    size_t index,
    uint32_t id
);

[[nodiscard]] std::expected<void, std::string> set_request_compress(
    cricodecs::cpk::Cpk& cpk,
    size_t index,
    bool request_compress
);

void set_all_request_compress(cricodecs::cpk::Cpk& cpk, bool request_compress);

[[nodiscard]] std::expected<void, std::string> set_user_string(
    cricodecs::cpk::Cpk& cpk,
    size_t index,
    std::string value
);

} // namespace cristudio::modules::cpk
