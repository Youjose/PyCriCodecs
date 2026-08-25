#pragma once
/**
 * @file cvm_container.hpp
 * @brief CVM / ROFS volume reader for classic CRI disc images.
 *
 * The payload is an ISO9660 tree after the `CVMH` and `ZONE` headers.
 * Scrambled TOCs can use a supplied key or recover the effective key.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cvm_key_recovery.hpp"
#include "../utilities/io_reader.hpp"

namespace cricodecs::cvm {

struct CvmHeader {
    uint64_t chunk_length = 0;
    uint64_t total_size = 0;
    std::array<uint8_t, 7> recording_date{};
    uint32_t flags = 0;
    std::string filesystem_id;
    std::string maker_id;
    uint32_t sector_table_entry_count = 0;
    uint32_t zone_sector_index = 0;
    uint32_t iso_start_sector = 0;
    std::vector<uint32_t> sector_table;
};

struct CvmZoneLayout {
    uint64_t chunk_length = 0;
    uint32_t zone_sector = 0;
    uint32_t sector_length_1 = 0;
    uint32_t sector_length_2 = 0;
    uint32_t data_sector = 0;
    uint64_t data_length = 0;
    uint32_t iso_sector = 0;
    uint64_t iso_length = 0;
};

struct CvmPrimaryVolume {
    std::string system_identifier;
    std::string volume_identifier;
    std::string volume_set_identifier;
    std::string publisher_identifier;
    std::string data_preparer_identifier;
    std::string application_identifier;
    uint32_t volume_space_size = 0;
    uint16_t logical_block_size = 0;
};

struct CvmEntry {
    uint32_t index = 0;
    std::filesystem::path path;
    uint32_t extent_sector = 0;
    uint32_t size = 0;
};

struct CvmDirectoryEntry {
    std::string name;
    std::filesystem::path archive_path;
    bool is_directory = false;
    uint64_t size = 0;
};

struct CvmDirectoryRecord {
    std::filesystem::path directory_path;
    uint32_t extent_sector = 0;
    uint32_t byte_size = 0;
    std::vector<CvmDirectoryEntry> entries;
};

class CvmContainer {
public:
    CvmContainer() = default;

    static constexpr uint32_t sector_length() noexcept { return 0x800u; }

    [[nodiscard]] static std::expected<CvmContainer, std::string> load(
        std::span<const uint8_t> data,
        std::string_view key = {}
    );
    [[nodiscard]] static std::expected<CvmContainer, std::string> load(
        std::vector<uint8_t>&& data,
        std::string_view key = {}
    );
    [[nodiscard]] static std::expected<CvmContainer, std::string> load(
        const std::filesystem::path& path,
        std::string_view key = {}
    );
    [[nodiscard]] static std::expected<CvmContainer, std::string> load(
        std::span<const uint8_t> data,
        const CvmKey& key
    );
    [[nodiscard]] static std::expected<CvmContainer, std::string> load(
        std::vector<uint8_t>&& data,
        const CvmKey& key
    );
    [[nodiscard]] static std::expected<CvmContainer, std::string> load(
        const std::filesystem::path& path,
        const CvmKey& key
    );

    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }
    [[nodiscard]] const std::string& disc_name() const noexcept { return m_disc_name; }
    [[nodiscard]] const std::string& recording_date_text() const noexcept { return m_recording_date_text; }
    [[nodiscard]] const std::string& media() const noexcept { return m_media; }
    [[nodiscard]] const CvmHeader& header() const noexcept { return m_header; }
    [[nodiscard]] const CvmZoneLayout& zone() const noexcept { return m_zone; }
    [[nodiscard]] const CvmPrimaryVolume& primary_volume() const noexcept { return m_primary_volume; }
    [[nodiscard]] bool is_scrambled() const noexcept { return (m_header.flags & 0x10u) != 0; }
    [[nodiscard]] bool has_accessible_contents() const noexcept { return m_contents_accessible; }
    [[nodiscard]] size_t embedded_iso_offset() const noexcept {
        return static_cast<size_t>(m_header.iso_start_sector) * sector_length();
    }
    [[nodiscard]] size_t embedded_iso_size() const noexcept { return static_cast<size_t>(m_zone.iso_length); }
    [[nodiscard]] uint32_t embedded_iso_sector_count() const noexcept {
        return static_cast<uint32_t>((embedded_iso_size() + sector_length() - 1u) / sector_length());
    }

    [[nodiscard]] uint32_t entry_count() const noexcept { return static_cast<uint32_t>(m_entries.size()); }
    [[nodiscard]] const std::vector<CvmEntry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] const CvmEntry& entry(uint32_t index) const { return m_entries[index]; }

    void set_disc_name(std::string value) { m_disc_name = std::move(value); }
    void set_recording_date(std::string value) { m_recording_date_text = std::move(value); invalidate_layout(); }
    [[nodiscard]] std::expected<void, std::string> set_media(std::string value);
    void set_system_identifier(std::string value) { m_primary_volume.system_identifier = std::move(value); invalidate_layout(); }
    void set_volume_identifier(std::string value) { m_primary_volume.volume_identifier = std::move(value); invalidate_layout(); }
    void set_volume_set_identifier(std::string value) { m_primary_volume.volume_set_identifier = std::move(value); invalidate_layout(); }
    void set_publisher_identifier(std::string value) { m_primary_volume.publisher_identifier = std::move(value); invalidate_layout(); }
    void set_data_preparer_identifier(std::string value) { m_primary_volume.data_preparer_identifier = std::move(value); invalidate_layout(); }
    void set_application_identifier(std::string value) { m_primary_volume.application_identifier = std::move(value); invalidate_layout(); }

    [[nodiscard]] const CvmEntry* find_entry(const std::filesystem::path& archive_path) const noexcept;
    [[nodiscard]] const CvmEntry* find_entry(
        const std::filesystem::path& relative_path,
        const CvmDirectoryRecord& directory
    ) const noexcept;
    [[nodiscard]] const CvmEntry* find_entry(
        const std::filesystem::path& runtime_path,
        std::string_view mounted_volume_name
    ) const noexcept;
    [[nodiscard]] std::expected<CvmDirectoryRecord, std::string> directory_record(
        const std::filesystem::path& archive_directory = {}
    ) const;
    [[nodiscard]] std::expected<CvmDirectoryRecord, std::string> directory_record_from_extent_sector(
        uint32_t extent_sector
    ) const;
    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> iso_directory_data(
        const std::filesystem::path& archive_directory = {}
    ) const;
    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> iso_directory_data_from_extent_sector(
        uint32_t extent_sector
    ) const;
    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> sector_range_data(
        uint32_t start_sector,
        uint32_t sector_count
    ) const;

    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> file_data(uint32_t index) const;
    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> file_data(
        const std::filesystem::path& archive_path
    ) const;
    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> file_data(
        const std::filesystem::path& relative_path,
        const CvmDirectoryRecord& directory
    ) const;
    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> file_data(
        const std::filesystem::path& runtime_path,
        std::string_view mounted_volume_name
    ) const;
    [[nodiscard]] std::expected<void, std::string> extract_file(
        uint32_t index,
        const std::filesystem::path& output_path
    ) const;
    [[nodiscard]] std::expected<void, std::string> extract(const std::filesystem::path& output_root) const {
        return extract_all(output_root);
    }
    [[nodiscard]] std::expected<void, std::string> extract(const CvmEntry& entry, const std::filesystem::path& output_root) const;
    [[nodiscard]] std::expected<void, std::string> extract_all(const std::filesystem::path& output_root) const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> save(std::string_view key = {}) const;
    [[nodiscard]] std::expected<void, std::string> save_to_file(
        const std::filesystem::path& output_path,
        std::string_view key = {}
    ) const;
    [[nodiscard]] std::expected<std::string, std::string> export_script_text() const;
    [[nodiscard]] std::expected<void, std::string> export_script_file(const std::filesystem::path& output_path) const;
    [[nodiscard]] std::expected<uint32_t, std::string> add_file(
        const std::filesystem::path& source_path,
        const std::filesystem::path& archive_path
    );
    [[nodiscard]] std::expected<uint32_t, std::string> add_bytes(
        std::span<const uint8_t> data,
        const std::filesystem::path& archive_path
    );
    [[nodiscard]] std::expected<void, std::string> replace_file(
        uint32_t index,
        const std::filesystem::path& source_path
    );
    [[nodiscard]] std::expected<void, std::string> replace_file(
        const std::filesystem::path& archive_path,
        const std::filesystem::path& source_path
    );
    [[nodiscard]] std::expected<void, std::string> replace_bytes(uint32_t index, std::span<const uint8_t> data);
    [[nodiscard]] std::expected<void, std::string> replace_bytes(
        const std::filesystem::path& archive_path,
        std::span<const uint8_t> data
    );
    [[nodiscard]] std::expected<void, std::string> remove(uint32_t index);
    [[nodiscard]] std::expected<void, std::string> remove(const std::filesystem::path& archive_path);
    [[nodiscard]] std::expected<void, std::string> move_file(uint32_t from_index, uint32_t to_index);
    [[nodiscard]] std::expected<void, std::string> rename(uint32_t index, const std::filesystem::path& archive_path);
    [[nodiscard]] std::expected<void, std::string> rename(
        const std::filesystem::path& existing_archive_path,
        const std::filesystem::path& archive_path
    );

private:
    struct EntryPayload {
        std::filesystem::path source_path;
        size_t source_offset = 0;
        std::optional<std::vector<uint8_t>> owned_bytes;
    };

    io::SourceView m_source;
    std::filesystem::path m_source_path;
    std::string m_disc_name;
    std::string m_recording_date_text;
    std::string m_media = "DVD";
    CvmHeader m_header;
    CvmZoneLayout m_zone;
    CvmPrimaryVolume m_primary_volume;
    std::vector<CvmEntry> m_entries;
    std::vector<EntryPayload> m_entry_payloads;
    std::vector<CvmDirectoryRecord> m_directories;
    bool m_contents_accessible = true;

    [[nodiscard]] static std::expected<CvmContainer, std::string> load_owned(
        std::vector<uint8_t>&& data,
        std::filesystem::path source_path,
        std::optional<CvmKey> key
    );
    [[nodiscard]] static std::expected<CvmContainer, std::string> load_source(
        io::SourceView source,
        std::filesystem::path source_path,
        std::optional<CvmKey> key);
    [[nodiscard]] static std::expected<CvmContainer, std::string> load_path(
        const std::filesystem::path& path,
        std::optional<CvmKey> key
    );
    [[nodiscard]] std::expected<void, std::string> parse(std::optional<CvmKey> key);
    [[nodiscard]] std::expected<uint32_t, std::string> index_of(const std::filesystem::path& archive_path) const;
    [[nodiscard]] std::expected<void, std::string> ensure_contents_accessible() const;
    [[nodiscard]] bool has_current_layout() const noexcept {
        return !m_contents_accessible || !m_directories.empty();
    }
    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> file_data_from_index(uint32_t index) const;
    void invalidate_layout();
    void reindex_entries();
};

} // namespace cricodecs::cvm
