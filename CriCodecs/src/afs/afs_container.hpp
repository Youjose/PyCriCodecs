#pragma once
/**
 * @file afs_container.hpp
 * @brief AFS - classic CRI archive container
 *
 * Ported from the legacy AFS reader shape and cross-checked against CRI's
 * `afslnk` tooling. Classic AFS archives use an `AFS\0` little-endian header,
 * an offset/size table, and may include an optional 0x30-byte-per-entry
 * directory table with filenames.
 */

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../utilities/io_reader.hpp"

namespace cricodecs::afs {

enum class AfsEntryType {
    unknown,
    adx,
    ogg,
    hca,
};

enum class AfsHeaderNameMode {
    filename_only,
    cut_overlapping_string,
    full_path,
};

struct AfsDirectoryTimestamp {
    uint16_t year = 0;
    uint16_t month = 0;
    uint16_t day = 0;
    uint16_t hour = 0;
    uint16_t minute = 0;
    uint16_t second = 0;

    [[nodiscard]] constexpr bool operator==(const AfsDirectoryTimestamp&) const noexcept = default;

    [[nodiscard]] constexpr bool empty() const noexcept {
        return *this == AfsDirectoryTimestamp{};
    }
};

[[nodiscard]] std::array<uint8_t, 12> encode_directory_timestamp(const AfsDirectoryTimestamp& timestamp) noexcept;

[[nodiscard]] constexpr const char* entry_extension(AfsEntryType type) noexcept {
    switch (type) {
        case AfsEntryType::adx: return ".adx";
        case AfsEntryType::ogg: return ".ogg";
        case AfsEntryType::hca: return ".hca";
        default: return ".bin";
    }
}

struct AfsEntry {
    // In classic AFS, the table slot is also the public file ID used by CRI's
    // SDK samples and generated headers.
    uint32_t index = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
    bool present = false;
    // Best-effort export hint for extensionless entries, not a semantic parser contract.
    AfsEntryType type = AfsEntryType::unknown;
    std::optional<std::string> name;
    // Optional original source-path text for generated file-ID headers.
    // Official CRI headers can preserve a full source path here even when the
    // on-disk directory record keeps only the basename.
    std::optional<std::string> header_source_name;
    // The optional directory record stores a 12-byte metadata payload after the
    // 32-byte filename; the final 4 bytes of the 0x30-byte record mirror size.
    std::array<uint8_t, 12> directory_metadata{};

    [[nodiscard]] bool is_present() const noexcept { return present; }
    [[nodiscard]] std::filesystem::path suggested_path(bool include_index_prefix = true) const;
    [[nodiscard]] std::optional<AfsDirectoryTimestamp> directory_timestamp() const noexcept;
};

class AfsContainer {
public:
    static constexpr uint32_t DEFAULT_ALIGNMENT = 0x800;

    AfsContainer() = default;

    [[nodiscard]] static std::expected<AfsContainer, std::string> load(std::span<const uint8_t> data);
    [[nodiscard]] static std::expected<AfsContainer, std::string> load(
        std::span<const uint8_t> data,
        io::SourceView::Owner owner
    );
    [[nodiscard]] static std::expected<AfsContainer, std::string> load(const std::filesystem::path& path);
    [[nodiscard]] static AfsContainer create(uint32_t alignment = DEFAULT_ALIGNMENT,
                                             bool include_directory_table = false);
    [[nodiscard]] static std::expected<AfsContainer, std::string> create_from_als(
        const std::filesystem::path& als_path,
        uint32_t alignment = DEFAULT_ALIGNMENT,
        bool include_directory_table = false,
        std::optional<std::filesystem::path> source_root = std::nullopt
    );

    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }
    [[nodiscard]] uint32_t alignment() const noexcept { return m_alignment; }
    [[nodiscard]] uint32_t present_entry_count() const noexcept;
    // Returns the on-disk slot count, which is also the highest file ID plus one.
    [[nodiscard]] uint32_t entry_count() const noexcept { return static_cast<uint32_t>(m_entries.size()); }
    [[nodiscard]] const std::vector<AfsEntry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] const AfsEntry& entry(uint32_t index) const { return m_entries[index]; }

    [[nodiscard]] bool has_directory_table() const noexcept { return m_directory_table_offset.has_value(); }
    [[nodiscard]] bool directory_table_enabled() const noexcept { return m_emit_directory_table; }
    [[nodiscard]] std::optional<uint32_t> directory_table_offset() const noexcept { return m_directory_table_offset; }
    [[nodiscard]] std::optional<uint32_t> directory_table_size() const noexcept { return m_directory_table_size; }
    [[nodiscard]] std::optional<uint32_t> first_payload_offset() const noexcept { return m_first_payload_offset; }

    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> file_data(uint32_t index) const;
    [[nodiscard]] std::expected<void, std::string> export_stream(uint32_t index, const std::filesystem::path& output_path) const;
    [[nodiscard]] std::expected<void, std::string> extract(const std::filesystem::path& output_dir) const;
    [[nodiscard]] std::expected<void, std::string> export_all(const std::filesystem::path& output_dir) const {
        return extract(output_dir);
    }
    void add_file(std::span<const uint8_t> data,
                  std::optional<std::string> name = std::nullopt,
                  const std::array<uint8_t, 12>& directory_metadata = {});
    void add_file_at_id(uint32_t file_id,
                        std::span<const uint8_t> data,
                        std::optional<std::string> name = std::nullopt,
                        const std::array<uint8_t, 12>& directory_metadata = {});
    void reserve_file_id(uint32_t file_id);
    [[nodiscard]] bool is_materialized() const noexcept;
    [[nodiscard]] std::expected<void, std::string> materialize();
    [[nodiscard]] std::expected<void, std::string> replace_file(uint32_t index, std::span<const uint8_t> data);
    [[nodiscard]] std::expected<void, std::string> remove_file(uint32_t index);
    [[nodiscard]] std::expected<void, std::string> move_file(uint32_t from_index, uint32_t to_index);
    [[nodiscard]] std::expected<void, std::string> set_name(uint32_t index, std::optional<std::string> name);
    [[nodiscard]] std::expected<void, std::string> set_header_source_name(
        uint32_t index,
        std::optional<std::string> header_source_name
    );
    [[nodiscard]] std::expected<void, std::string> set_directory_metadata(
        uint32_t index,
        const std::array<uint8_t, 12>& metadata
    );
    [[nodiscard]] std::expected<void, std::string> set_directory_timestamp(
        uint32_t index,
        std::optional<AfsDirectoryTimestamp> timestamp
    );
    [[nodiscard]] std::expected<void, std::string> set_alignment(uint32_t alignment);
    [[nodiscard]] std::expected<void, std::string> set_first_payload_offset(std::optional<uint32_t> offset);
    void set_directory_table_enabled(bool enabled) noexcept { m_emit_directory_table = enabled; }
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build();
    [[nodiscard]] std::expected<void, std::string> build_to_file(const std::filesystem::path& output_path);
    [[nodiscard]] std::expected<std::string, std::string> build_file_id_header(
        std::string_view archive_name,
        std::string_view id_prefix = {},
        AfsHeaderNameMode name_mode = AfsHeaderNameMode::filename_only
    ) const;

private:
    io::SourceView m_source;
    std::filesystem::path m_source_path;
    std::vector<AfsEntry> m_entries;
    std::vector<std::optional<std::vector<uint8_t>>> m_payloads;
    std::optional<uint32_t> m_directory_table_offset;
    std::optional<uint32_t> m_directory_table_size;
    std::optional<uint32_t> m_first_payload_offset;
    uint32_t m_alignment = DEFAULT_ALIGNMENT;
    bool m_emit_directory_table = false;

    [[nodiscard]] std::expected<void, std::string> parse();
    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> build_payload(uint32_t index) const;
};

} // namespace cricodecs::afs
