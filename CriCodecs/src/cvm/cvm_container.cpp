/**
 * @file cvm_container.cpp
 * @brief CVM/ROFS container object helpers.
 *
 * The object keeps the format fields needed for inspection and rebuilding,
 * while payloads remain borrowed until an entry is changed.
 */

#include "cvm_container.hpp"

#include <algorithm>
#include <flat_map>
#include <fstream>
#include <string_view>

#include "cvm_build_script.hpp"
#include "cvm_builder.hpp"
#include "cvm_path.hpp"
#include "../utilities/io.hpp"
#include "../utilities/numeric.hpp"
#include "../utilities/string.hpp"

namespace cricodecs::cvm {

namespace {

using util::align_up;
using util::uppercase_ascii;

constexpr size_t sector_size = CvmContainer::sector_length();

[[nodiscard]] bool is_same_archive_directory(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
    return normalize_archive_lookup_key(lhs) == normalize_archive_lookup_key(rhs);
}

[[nodiscard]] std::optional<std::string> normalize_runtime_archive_path(
    const std::filesystem::path& runtime_path,
    std::string_view mounted_volume_name
) {
    std::string normalized = normalize_archive_path(runtime_path);
    if (normalized.empty()) {
        return std::string{};
    }

    const size_t volume_delimiter = normalized.find(':');
    if (volume_delimiter == std::string::npos) {
        return uppercase_ascii(std::move(normalized));
    }

    const std::string runtime_volume = uppercase_ascii(normalized.substr(0, volume_delimiter));
    if (!mounted_volume_name.empty() && runtime_volume != uppercase_ascii(std::string(mounted_volume_name))) {
        return std::nullopt;
    }

    normalized.erase(0, volume_delimiter + 1);
    normalized.erase(0, normalized.find_first_not_of('/'));
    return uppercase_ascii(std::move(normalized));
}

[[nodiscard]] std::expected<void, std::string> write_output_file(
    const std::filesystem::path& path,
    std::span<const uint8_t> data,
    std::string_view context
) {
    if (path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return std::unexpected(std::string(context) + ": could not create output directory: " + error.message());
        }
    }
    return io::write_file_bytes(path, data, context);
}

} // namespace

std::expected<void, std::string> CvmContainer::ensure_contents_accessible() const {
    if (!m_contents_accessible) {
        return std::unexpected("CVM contents require a scramble key before file inventory or extraction is available");
    }
    return {};
}

void CvmContainer::invalidate_layout() {
    m_directories.clear();
    for (auto& entry : m_entries) {
        entry.extent_sector = 0;
    }
}

void CvmContainer::reindex_entries() {
    for (size_t index = 0; index < m_entries.size(); ++index) {
        m_entries[index].index = static_cast<uint32_t>(index);
    }
}

std::expected<void, std::string> CvmContainer::set_media(std::string value) {
    value = uppercase_ascii(std::move(value));
    if (value != "CD" && value != "DVD") {
        return std::unexpected("CVM media must be one of the official ROFS values: CD or DVD");
    }
    m_media = std::move(value);
    invalidate_layout();
    return {};
}

std::expected<uint32_t, std::string> CvmContainer::index_of(const std::filesystem::path& archive_path) const {
    const std::string normalized_path = normalize_archive_lookup_key(archive_path);
    if (normalized_path.empty()) {
        return std::unexpected("CVM archive path must not be empty");
    }
    for (size_t index = 0; index < m_entries.size(); ++index) {
        if (normalize_archive_lookup_key(m_entries[index].path) == normalized_path) {
            return static_cast<uint32_t>(index);
        }
    }
    return std::unexpected("CVM entry path was not found: " + normalize_archive_path(archive_path));
}

const CvmEntry* CvmContainer::find_entry(const std::filesystem::path& archive_path) const noexcept {
    const std::string normalized_path = normalize_archive_lookup_key(archive_path);
    if (normalized_path.empty()) {
        return nullptr;
    }
    for (const auto& entry : m_entries) {
        if (normalize_archive_lookup_key(entry.path) == normalized_path) {
            return &entry;
        }
    }
    return nullptr;
}

const CvmEntry* CvmContainer::find_entry(
    const std::filesystem::path& relative_path,
    const CvmDirectoryRecord& directory
) const noexcept {
    return find_entry(resolve_directory_relative_path(directory.directory_path, relative_path));
}

const CvmEntry* CvmContainer::find_entry(
    const std::filesystem::path& runtime_path,
    std::string_view mounted_volume_name
) const noexcept {
    const auto normalized = normalize_runtime_archive_path(runtime_path, mounted_volume_name);
    if (!normalized.has_value()) {
        return nullptr;
    }
    return find_entry(*normalized);
}

std::expected<CvmDirectoryRecord, std::string> CvmContainer::directory_record(
    const std::filesystem::path& archive_directory
) const {
    if (auto accessible = ensure_contents_accessible(); !accessible) {
        return std::unexpected(accessible.error());
    }

    const std::filesystem::path normalized_directory = normalize_archive_path(archive_directory);
    const bool root_directory = is_root_archive_path(normalized_directory);
    bool directory_exists = root_directory;
    std::optional<std::filesystem::path> canonical_directory_path;
    if (root_directory) {
        canonical_directory_path.emplace();
    }
    std::flat_map<std::string, CvmDirectoryEntry> directory_entries;

    for (const auto& entry : m_entries) {
        const std::filesystem::path parent = entry.path.parent_path();
        std::optional<std::filesystem::path> child_directory;
        std::filesystem::path ancestor = parent;

        if (root_directory) {
            while (!ancestor.empty() && !ancestor.parent_path().empty()) {
                ancestor = ancestor.parent_path();
            }
            if (!ancestor.empty()) {
                child_directory = ancestor;
            }
        } else {
            while (!ancestor.empty() && !is_same_archive_directory(ancestor, normalized_directory)) {
                child_directory = ancestor;
                ancestor = ancestor.parent_path();
            }
            if (ancestor.empty()) {
                continue;
            }
            canonical_directory_path = ancestor;
        }

        directory_exists = true;
        if (child_directory) {
            const std::string name = child_directory->filename().generic_string();
            directory_entries.try_emplace(
                normalize_archive_lookup_key(name),
                CvmDirectoryEntry{
                    .name = name,
                    .archive_path = *child_directory,
                    .is_directory = true,
                    .size = 0,
                }
            );
        } else {
            const std::string name = entry.path.filename().generic_string();
            directory_entries.emplace(
                normalize_archive_lookup_key(name),
                CvmDirectoryEntry{name, entry.path, false, entry.size}
            );
        }
    }

    if (!directory_exists) {
        return std::unexpected("CVM directory path was not found: " + normalize_archive_path(normalized_directory));
    }

    CvmDirectoryRecord record;
    record.directory_path = canonical_directory_path.value_or(normalized_directory);
    if (has_current_layout()) {
        for (const auto& directory : m_directories) {
            if (is_same_archive_directory(directory.directory_path, record.directory_path)) {
                record.extent_sector = directory.extent_sector;
                record.byte_size = directory.byte_size;
                break;
            }
        }
    }
    for (auto&& [_, directory_entry] : directory_entries) {
        record.entries.push_back(std::move(directory_entry));
    }
    std::sort(record.entries.begin(), record.entries.end(), [](const CvmDirectoryEntry& lhs, const CvmDirectoryEntry& rhs) {
        if (lhs.is_directory != rhs.is_directory) {
            return lhs.is_directory > rhs.is_directory;
        }
        return uppercase_ascii(lhs.name) < uppercase_ascii(rhs.name);
    });
    return record;
}

std::expected<CvmDirectoryRecord, std::string> CvmContainer::directory_record_from_extent_sector(uint32_t extent_sector) const {
    if (auto accessible = ensure_contents_accessible(); !accessible) {
        return std::unexpected(accessible.error());
    }
    if (!has_current_layout()) {
        return std::unexpected("CVM raw ISO layout is unavailable after unsaved mutations");
    }
    for (const auto& directory : m_directories) {
        if (directory.extent_sector == extent_sector) {
            return directory_record(directory.directory_path);
        }
    }
    return std::unexpected("CVM directory extent sector was not found: " + std::to_string(extent_sector));
}

std::expected<std::span<const uint8_t>, std::string> CvmContainer::iso_directory_data(
    const std::filesystem::path& archive_directory
) const {
    if (!has_current_layout()) {
        return std::unexpected("CVM raw ISO layout is unavailable after unsaved mutations");
    }
    auto directory = directory_record(archive_directory);
    if (!directory) {
        return std::unexpected(directory.error());
    }
    return iso_directory_data_from_extent_sector(directory->extent_sector);
}

std::expected<std::span<const uint8_t>, std::string> CvmContainer::iso_directory_data_from_extent_sector(
    uint32_t extent_sector
) const {
    if (!has_current_layout()) {
        return std::unexpected("CVM raw ISO layout is unavailable after unsaved mutations");
    }
    auto directory = directory_record_from_extent_sector(extent_sector);
    if (!directory) {
        return std::unexpected(directory.error());
    }

    const size_t offset = embedded_iso_offset() + static_cast<size_t>(directory->extent_sector) * sector_size;
    const size_t padded_size = align_up(directory->byte_size, sector_size);
    if (offset > m_source.size() || padded_size > m_source.size() - offset) {
        return std::unexpected("CVM ISO directory record data is out of bounds");
    }

    return m_source.subspan(offset, padded_size);
}

std::expected<std::span<const uint8_t>, std::string> CvmContainer::sector_range_data(
    uint32_t start_sector,
    uint32_t sector_count
) const {
    if (auto accessible = ensure_contents_accessible(); !accessible) {
        return std::unexpected(accessible.error());
    }
    if (!has_current_layout()) {
        return std::unexpected("CVM raw ISO layout is unavailable after unsaved mutations");
    }

    const size_t iso_offset = embedded_iso_offset();
    const size_t byte_offset = iso_offset + static_cast<size_t>(start_sector) * sector_size;
    const size_t byte_size = static_cast<size_t>(sector_count) * sector_size;
    const size_t iso_end = iso_offset + embedded_iso_size();

    if (byte_offset < iso_offset || byte_offset > iso_end) {
        return std::unexpected("CVM sector range starts outside the embedded ISO span");
    }
    if (byte_size > iso_end - byte_offset) {
        return std::unexpected("CVM sector range exceeds the embedded ISO span");
    }
    if (byte_offset > m_source.size() || byte_size > m_source.size() - byte_offset) {
        return std::unexpected("CVM sector range data is out of bounds");
    }
    return m_source.subspan(byte_offset, byte_size);
}

std::expected<std::span<const uint8_t>, std::string> CvmContainer::file_data_from_index(uint32_t index) const {
    if (index >= m_entries.size()) {
        return std::unexpected("CVM entry index is out of range");
    }

    const auto& entry = m_entries[index];
    const auto& payload = m_entry_payloads[index];
    if (!payload.owned_bytes) {
        if (payload.source_offset > m_source.size() || entry.size > m_source.size() - payload.source_offset) {
            return std::unexpected("CVM entry data is out of bounds");
        }
        return m_source.subspan(payload.source_offset, entry.size);
    }
    return std::span<const uint8_t>(*payload.owned_bytes);
}

std::expected<std::span<const uint8_t>, std::string> CvmContainer::file_data(uint32_t index) const {
    if (auto accessible = ensure_contents_accessible(); !accessible) {
        return std::unexpected(accessible.error());
    }
    return file_data_from_index(index);
}

std::expected<std::span<const uint8_t>, std::string> CvmContainer::file_data(
    const std::filesystem::path& archive_path
) const {
    if (auto accessible = ensure_contents_accessible(); !accessible) {
        return std::unexpected(accessible.error());
    }
    auto index = index_of(archive_path);
    if (!index) {
        return std::unexpected(index.error());
    }
    return file_data_from_index(*index);
}

std::expected<std::span<const uint8_t>, std::string> CvmContainer::file_data(
    const std::filesystem::path& relative_path,
    const CvmDirectoryRecord& directory
) const {
    if (auto accessible = ensure_contents_accessible(); !accessible) {
        return std::unexpected(accessible.error());
    }

    const std::filesystem::path resolved_path = resolve_directory_relative_path(directory.directory_path, relative_path);
    auto index = index_of(resolved_path);
    if (!index) {
        return std::unexpected(
            "CVM entry path was not found relative to directory '" +
            normalize_archive_path(directory.directory_path) + "': " + normalize_archive_path(relative_path)
        );
    }
    return file_data_from_index(*index);
}

std::expected<std::span<const uint8_t>, std::string> CvmContainer::file_data(
    const std::filesystem::path& runtime_path,
    std::string_view mounted_volume_name
) const {
    if (auto accessible = ensure_contents_accessible(); !accessible) {
        return std::unexpected(accessible.error());
    }
    const auto normalized = normalize_runtime_archive_path(runtime_path, mounted_volume_name);
    if (!normalized.has_value()) {
        return std::unexpected(
            "CVM runtime path volume did not match mounted volume '" + std::string(mounted_volume_name) +
            "': " + runtime_path.generic_string()
        );
    }
    return file_data(*normalized);
}

std::expected<void, std::string> CvmContainer::extract(
    const CvmEntry& entry,
    const std::filesystem::path& output_root
) const {
    auto data = file_data(entry.index);
    if (!data) {
        return std::unexpected(data.error());
    }

    return write_output_file(output_root / entry.path, *data, "CVM extract failed");
}

std::expected<void, std::string> CvmContainer::extract_file(
    uint32_t index,
    const std::filesystem::path& output_path
) const {
    auto data = file_data(index);
    if (!data) {
        return std::unexpected(data.error());
    }

    return write_output_file(output_path, *data, "CVM extract failed");
}

std::expected<void, std::string> CvmContainer::extract_all(const std::filesystem::path& output_root) const {
    std::error_code filesystem_error;
    std::filesystem::create_directories(output_root, filesystem_error);
    if (filesystem_error) {
        return std::unexpected("CVM extract failed: could not create output root: " + filesystem_error.message());
    }

    for (const auto& entry : m_entries) {
        auto exported = extract(entry, output_root);
        if (!exported) {
            return std::unexpected(exported.error());
        }
    }
    return {};
}

std::expected<std::vector<uint8_t>, std::string> CvmContainer::save(std::string_view key) const {
    if (auto accessible = ensure_contents_accessible(); !accessible) {
        return std::unexpected(accessible.error());
    }

    CvmBuildInput input;
    input.disc_name = m_disc_name.empty() ? default_disc_name(m_source_path, m_primary_volume.volume_identifier) : m_disc_name;
    input.recording_date = m_recording_date_text;
    input.media = m_media.empty() ? "DVD" : m_media;
    input.system_identifier = m_primary_volume.system_identifier.empty() ? "CRI ROFS" : m_primary_volume.system_identifier;
    input.volume_identifier = m_primary_volume.volume_identifier;
    input.volume_set_identifier = m_primary_volume.volume_set_identifier;
    input.publisher_identifier = m_primary_volume.publisher_identifier;
    input.data_preparer_identifier = m_primary_volume.data_preparer_identifier;
    input.application_identifier = m_primary_volume.application_identifier;
    input.files.reserve(m_entries.size());
    input.preserve_file_order = true;

    for (size_t index = 0; index < m_entries.size(); ++index) {
        auto data = file_data_from_index(static_cast<uint32_t>(index));
        if (!data) {
            return std::unexpected(data.error());
        }
        CvmBuildFile file;
        file.archive_path = m_entries[index].path;
        file.source_path = m_entry_payloads[index].source_path;
        file.data_span = *data;
        input.files.push_back(std::move(file));
    }

    return CvmBuilder{}.build(input, key);
}

std::expected<void, std::string> CvmContainer::save_to_file(
    const std::filesystem::path& output_path,
    std::string_view key
) const {
    auto bytes = save(key);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    return write_output_file(output_path, *bytes, "CVM save failed");
}

std::expected<std::string, std::string> CvmContainer::export_script_text() const {
    if (auto accessible = ensure_contents_accessible(); !accessible) {
        return std::unexpected(accessible.error());
    }

    CvmBuildScriptExport script{
        .disc_name = m_disc_name.empty() ? default_disc_name(m_source_path, m_primary_volume.volume_identifier) : m_disc_name,
        .recording_date = m_recording_date_text,
        .media = m_media.empty() ? "DVD" : m_media,
        .system_identifier = m_primary_volume.system_identifier.empty() ? "CRI ROFS" : m_primary_volume.system_identifier,
        .volume_identifier = m_primary_volume.volume_identifier,
        .volume_set_identifier = m_primary_volume.volume_set_identifier,
        .publisher_identifier = m_primary_volume.publisher_identifier,
        .data_preparer_identifier = m_primary_volume.data_preparer_identifier,
        .application_identifier = m_primary_volume.application_identifier,
        .define_root = std::nullopt,
        .files = {},
    };

    const std::string disc_stem = std::filesystem::path(script.disc_name).stem().generic_string().empty()
        ? std::string("image")
        : std::filesystem::path(script.disc_name).stem().generic_string();
    bool needs_define_root = false;

    script.files.reserve(m_entries.size());
    for (size_t index = 0; index < m_entries.size(); ++index) {
        std::filesystem::path source_path = m_entry_payloads[index].source_path;
        if (source_path.empty()) {
            needs_define_root = true;
            source_path = std::filesystem::path("[CVM_ROOT]") / m_entries[index].path;
        }
        script.files.push_back({
            .archive_path = m_entries[index].path,
            .source_path = source_path,
        });
    }

    if (needs_define_root) {
        script.define_root = std::pair<std::string, std::filesystem::path>{"CVM_ROOT", disc_stem};
    }
    return format_cvm_build_script(script);
}

std::expected<void, std::string> CvmContainer::export_script_file(const std::filesystem::path& output_path) const {
    auto text = export_script_text();
    if (!text) {
        return std::unexpected(text.error());
    }

    std::error_code filesystem_error;
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
        if (filesystem_error) {
            return std::unexpected("CVM script export failed: could not create output directory: " + filesystem_error.message());
        }
    }

    std::ofstream file(output_path);
    if (!file) {
        return std::unexpected("CVM script export failed: could not open output: " + output_path.string());
    }
    file << *text;
    if (!file) {
        return std::unexpected("CVM script export failed: could not write output: " + output_path.string());
    }
    return {};
}

std::expected<uint32_t, std::string> CvmContainer::add_file(
    const std::filesystem::path& source_path,
    const std::filesystem::path& archive_path
) {
    auto bytes = io::read_file_bytes(source_path, "CVM add_file failed");
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto added = add_bytes(std::span<const uint8_t>(bytes->data(), bytes->size()), archive_path);
    if (!added) {
        return std::unexpected(added.error());
    }
    m_entry_payloads[*added].source_path = source_path;
    return *added;
}

std::expected<uint32_t, std::string> CvmContainer::add_bytes(
    std::span<const uint8_t> data,
    const std::filesystem::path& archive_path
) {
    if (auto accessible = ensure_contents_accessible(); !accessible) {
        return std::unexpected(accessible.error());
    }
    if (find_entry(archive_path) != nullptr) {
        return std::unexpected("CVM add_file failed: duplicate archive path: " + normalize_archive_path(archive_path));
    }

    m_entries.push_back({
        .index = static_cast<uint32_t>(m_entries.size()),
        .path = std::filesystem::path(normalize_archive_path(archive_path)),
        .extent_sector = 0,
        .size = static_cast<uint32_t>(data.size()),
    });
    m_entry_payloads.push_back({
        .source_path = {},
        .source_offset = 0,
        .owned_bytes = std::vector<uint8_t>(data.begin(), data.end()),
    });
    invalidate_layout();
    return static_cast<uint32_t>(m_entries.size() - 1);
}

std::expected<void, std::string> CvmContainer::replace_file(
    uint32_t index,
    const std::filesystem::path& source_path
) {
    auto bytes = io::read_file_bytes(source_path, "CVM replace_file failed");
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto replaced = replace_bytes(index, std::span<const uint8_t>(bytes->data(), bytes->size()));
    if (!replaced) {
        return replaced;
    }
    m_entry_payloads[index].source_path = source_path;
    return {};
}

std::expected<void, std::string> CvmContainer::replace_file(
    const std::filesystem::path& archive_path,
    const std::filesystem::path& source_path
) {
    auto index = index_of(archive_path);
    if (!index) {
        return std::unexpected(index.error());
    }
    return replace_file(*index, source_path);
}

std::expected<void, std::string> CvmContainer::replace_bytes(uint32_t index, std::span<const uint8_t> data) {
    if (auto accessible = ensure_contents_accessible(); !accessible) {
        return std::unexpected(accessible.error());
    }
    if (index >= m_entries.size()) {
        return std::unexpected("CVM entry index is out of range");
    }
    m_entries[index].size = static_cast<uint32_t>(data.size());
    m_entry_payloads[index] = {
        .source_path = {},
        .source_offset = 0,
        .owned_bytes = std::vector<uint8_t>(data.begin(), data.end()),
    };
    invalidate_layout();
    return {};
}

std::expected<void, std::string> CvmContainer::replace_bytes(
    const std::filesystem::path& archive_path,
    std::span<const uint8_t> data
) {
    auto index = index_of(archive_path);
    if (!index) {
        return std::unexpected(index.error());
    }
    return replace_bytes(*index, data);
}

std::expected<void, std::string> CvmContainer::remove(uint32_t index) {
    if (auto accessible = ensure_contents_accessible(); !accessible) {
        return std::unexpected(accessible.error());
    }
    if (index >= m_entries.size()) {
        return std::unexpected("CVM entry index is out of range");
    }
    m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(index));
    m_entry_payloads.erase(m_entry_payloads.begin() + static_cast<std::ptrdiff_t>(index));
    invalidate_layout();
    reindex_entries();
    return {};
}

std::expected<void, std::string> CvmContainer::remove(const std::filesystem::path& archive_path) {
    auto index = index_of(archive_path);
    if (!index) {
        return std::unexpected(index.error());
    }
    return remove(*index);
}

std::expected<void, std::string> CvmContainer::move_file(uint32_t from_index, uint32_t to_index) {
    if (auto accessible = ensure_contents_accessible(); !accessible) {
        return std::unexpected(accessible.error());
    }
    if (from_index >= m_entries.size() || to_index >= m_entries.size()) {
        return std::unexpected("CVM entry index is out of range");
    }
    if (from_index == to_index) {
        return {};
    }

    auto entry = std::move(m_entries[from_index]);
    auto payload = std::move(m_entry_payloads[from_index]);
    m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(from_index));
    m_entry_payloads.erase(m_entry_payloads.begin() + static_cast<std::ptrdiff_t>(from_index));
    m_entries.insert(m_entries.begin() + static_cast<std::ptrdiff_t>(to_index), std::move(entry));
    m_entry_payloads.insert(m_entry_payloads.begin() + static_cast<std::ptrdiff_t>(to_index), std::move(payload));
    invalidate_layout();
    reindex_entries();
    return {};
}

std::expected<void, std::string> CvmContainer::rename(uint32_t index, const std::filesystem::path& archive_path) {
    if (auto accessible = ensure_contents_accessible(); !accessible) {
        return std::unexpected(accessible.error());
    }
    if (index >= m_entries.size()) {
        return std::unexpected("CVM entry index is out of range");
    }
    const std::string normalized = normalize_archive_path(archive_path);
    if (normalized.empty()) {
        return std::unexpected("CVM archive path must not be empty");
    }
    if (const auto* duplicate = find_entry(normalized); duplicate && duplicate->index != index) {
        return std::unexpected("CVM rename failed: duplicate archive path: " + normalized);
    }
    m_entries[index].path = normalized;
    invalidate_layout();
    return {};
}

std::expected<void, std::string> CvmContainer::rename(
    const std::filesystem::path& existing_archive_path,
    const std::filesystem::path& archive_path
) {
    auto index = index_of(existing_archive_path);
    if (!index) {
        return std::unexpected(index.error());
    }
    return rename(*index, archive_path);
}

} // namespace cricodecs::cvm
