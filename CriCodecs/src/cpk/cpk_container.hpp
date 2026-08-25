#pragma once
/**
 * @file cpk_container.hpp
 * @brief CPK archive container API.
 *
 * Format behavior follows CRI's CPK library. C++23 object model by Youjose.
 */

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "../utf/utf_table.hpp"
#include "../utilities/io.hpp"
#include "../utilities/text_encoding.hpp"

namespace cricodecs::cpk {

enum class CpkPreset : int32_t {
    Custom = -1,
    Id = 0,
    Filename = 1,
    FilenameId = 2,
    FilenameGroup = 3,
    IdGroup = 4,
    FilenameIdGroup = 5
};

[[nodiscard]] constexpr CpkPreset preset_from_chunks(
    bool has_toc,
    bool has_itoc,
    bool has_gtoc,
    bool has_etoc
) noexcept {
    if (!has_toc && has_itoc && !has_gtoc && !has_etoc) {
        return CpkPreset::Id;
    }
    if (has_toc && !has_itoc && !has_gtoc && has_etoc) {
        return CpkPreset::Filename;
    }
    if (has_toc && has_itoc && !has_gtoc && has_etoc) {
        return CpkPreset::FilenameId;
    }
    if (has_toc && !has_itoc && has_gtoc && has_etoc) {
        return CpkPreset::FilenameGroup;
    }
    if (has_toc && has_itoc && has_gtoc && !has_etoc) {
        return CpkPreset::IdGroup;
    }
    if (has_toc && has_itoc && has_gtoc && has_etoc) {
        return CpkPreset::FilenameIdGroup;
    }
    return CpkPreset::Custom;
}

enum class CpkMode {
    Mode0 = 0, // ITOC
    Mode1 = 1, // TOC
    Mode2 = 2, // TOC + ITOC
    Mode3 = 3  // TOC + ITOC + GTOC
};

struct CpkEntry {
    std::string dirname;
    std::string dirname_raw;
    std::string filename;
    std::string filename_raw;
    uint32_t id = 0;
    uint32_t toc_index = 0;

    uint64_t file_offset = 0;
    uint64_t file_size = 0;
    uint64_t extract_size = 0;
    bool is_compressed = false;
    bool request_compress = false;

    std::string user_string = "<NULL>";

    [[nodiscard]] std::filesystem::path full_path() const {
        std::filesystem::path path;
        if (!dirname.empty()) {
            path /= dirname;
        }
        if (!filename.empty()) {
            path /= filename;
        } else {
            path /= std::to_string(id);
        }
        return path;
    }
};

struct CpkOptions {
    // Official preset metadata and default chunk selection. Explicit enable_* overrides may
    // still produce a Custom layout when needed for odd real-world archives.
    CpkPreset preset = CpkPreset::Filename;
    std::optional<bool> enable_toc;
    std::optional<bool> enable_itoc;
    std::optional<bool> enable_gtoc;
    std::optional<bool> enable_etoc;
    bool enable_crc = false;
    uint16_t align = 0x800;
    text::EncodingOptions encoding;
    std::string tver;
    std::string comment = "<NULL>";
    std::string etoc_local_dir;
};

using CpkBuilderOptions = CpkOptions;

class Cpk {
public:
    Cpk() = default;
    Cpk(const Cpk&) = delete;
    Cpk& operator=(const Cpk&) = delete;
    Cpk(Cpk&&) noexcept = default;
    Cpk& operator=(Cpk&&) noexcept = default;

    [[nodiscard]] static std::expected<Cpk, std::string> load(
        const std::filesystem::path& path,
        const text::EncodingOptions& encoding = {}
    );
    [[nodiscard]] static std::expected<Cpk, std::string> load(
        std::span<const uint8_t> data,
        const text::EncodingOptions& encoding = {}
    );
    [[nodiscard]] static std::expected<Cpk, std::string> load(
        std::vector<uint8_t>&& data,
        const text::EncodingOptions& encoding = {}
    );
    [[nodiscard]] static Cpk create(const CpkOptions& options = {});

    std::expected<void, std::string> load_from_path(const std::filesystem::path& path);
    std::expected<void, std::string> load_from_bytes(std::span<const uint8_t> data);
    std::expected<void, std::string> load_from_bytes(std::vector<uint8_t>&& data);

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> save();
    [[nodiscard]] std::expected<void, std::string> save_to_file(const std::filesystem::path& output_path);
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> decrypt();
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> encrypt();

    void add_file(
        const std::filesystem::path& local_path,
        const std::string& cpk_path,
        bool compress = false,
        std::optional<uint32_t> id = std::nullopt
    );
    void add_bytes(
        std::span<const uint8_t> bytes,
        const std::string& cpk_path,
        bool compress = false,
        std::optional<uint32_t> id = std::nullopt
    );
    std::expected<void, std::string> remove(size_t index);
    std::expected<void, std::string> move_file(size_t from_index, size_t to_index);
    std::expected<void, std::string> rename(size_t index, const std::string& cpk_path);
    std::expected<void, std::string> set_dirname(size_t index, const std::string& dirname);
    std::expected<void, std::string> set_filename(size_t index, const std::string& filename);
    std::expected<void, std::string> set_request_compress(size_t index, bool compress);
    void set_all_request_compress(bool compress) noexcept;
    std::expected<void, std::string> replace_file(
        size_t index,
        const std::filesystem::path& local_path,
        std::optional<bool> compress = std::nullopt
    );
    std::expected<void, std::string> replace_bytes(
        size_t index,
        std::span<const uint8_t> bytes,
        std::optional<bool> compress = std::nullopt
    );

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> extract_to_memory(const CpkEntry& entry) const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> file_bytes(size_t index) const;
    std::expected<void, std::string> extract(const CpkEntry& entry, const std::filesystem::path& output_dir) const;
    std::expected<void, std::string> extract(
        const std::filesystem::path& output_dir,
        bool disambiguate_conflicts = true
    ) const;
    std::expected<void, std::string> extract_all(
        const std::filesystem::path& output_dir,
        bool disambiguate_conflicts = true
    ) const {
        return extract(output_dir, disambiguate_conflicts);
    }

    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }
    [[nodiscard]] CpkMode layout_mode() const noexcept {
        if (!has_toc()) return has_itoc() ? CpkMode::Mode0 : CpkMode::Mode1;
        if (!has_itoc()) return CpkMode::Mode1;
        return has_gtoc() ? CpkMode::Mode3 : CpkMode::Mode2;
    }
    [[nodiscard]] CpkMode mode() const noexcept { return layout_mode(); } // Legacy alias.
    [[nodiscard]] CpkPreset preset() const noexcept {
        if (!has_toc() && !has_itoc() && !has_gtoc() && !has_etoc()) {
            return m_options.preset;
        }
        return preset_from_chunks(has_toc(), has_itoc(), has_gtoc(), has_etoc());
    }
    [[nodiscard]] bool has_declared_preset() const noexcept { return m_declared_preset.has_value(); }
    [[nodiscard]] CpkPreset declared_preset() const noexcept {
        return m_declared_preset.value_or(CpkPreset::Custom);
    }
    [[nodiscard]] uint64_t content_offset() const;
    [[nodiscard]] uint16_t alignment() const noexcept { return m_options.align; }
    [[nodiscard]] const std::vector<CpkEntry>& files() const noexcept { return m_files; }
    [[nodiscard]] size_t file_count() const noexcept { return m_files.size(); }
    [[nodiscard]] bool has_toc() const noexcept { return m_toc.is_loaded(); }
    [[nodiscard]] bool has_itoc() const noexcept { return m_itoc.is_loaded(); }
    [[nodiscard]] bool has_gtoc() const noexcept { return m_gtoc.is_loaded(); }
    [[nodiscard]] bool has_etoc() const noexcept { return m_etoc.is_loaded(); }
    [[nodiscard]] const utf::UtfTable& cpk_header() const noexcept { return m_cpk_header; }
    [[nodiscard]] const utf::UtfTable& toc() const noexcept { return m_toc; }
    [[nodiscard]] const utf::UtfTable& itoc() const noexcept { return m_itoc; }
    [[nodiscard]] const utf::UtfTable& gtoc() const noexcept { return m_gtoc; }
    [[nodiscard]] const utf::UtfTable& etoc() const noexcept { return m_etoc; }
    [[nodiscard]] const CpkOptions& options() const noexcept { return m_options; }
    [[nodiscard]] CpkEntry* try_file(size_t index) noexcept;
    [[nodiscard]] const CpkEntry* try_file(size_t index) const noexcept;
    [[nodiscard]] CpkEntry& edit_file(size_t index);
    [[nodiscard]] CpkOptions& edit_options() noexcept;
    void set_options(const CpkOptions& options);
    void set_encoding(const text::EncodingOptions& encoding);

private:
    static constexpr uint32_t chunk_header_size = 0x10;
    static constexpr uint32_t chunk_alignment = 0x800;
    static constexpr uint64_t root_chunk_size = 0x800;
    static constexpr std::string_view default_tool_version = "CriCodecs CPK";

    struct ArchiveSource {};

    struct EntrySource {
        std::variant<ArchiveSource, std::filesystem::path, std::vector<uint8_t>> data;
        std::optional<uint32_t> explicit_id;
    };

    struct PreparedEntry {
        uint32_t effective_id = 0;
        uint64_t unpacked_size = 0;
        uint32_t crc32 = 0;
        std::vector<uint8_t> owned_payload;
        std::span<const uint8_t> payload;
    };

    std::filesystem::path m_source_path;
    io::SourceView m_source;

    utf::UtfTable m_cpk_header;
    utf::UtfTable m_toc;
    utf::UtfTable m_itoc;
    utf::UtfTable m_gtoc;
    utf::UtfTable m_etoc;

    std::optional<CpkPreset> m_declared_preset;
    CpkOptions m_options;
    bool m_dirty = false;

    std::vector<CpkEntry> m_files;
    std::vector<EntrySource> m_sources;

    void add_source(
        EntrySource source,
        const std::string& cpk_path,
        bool compress
    );
    std::expected<void, std::string> replace_source(
        size_t index,
        EntrySource source,
        std::optional<bool> compress
    );
    std::expected<void, std::string> parse();
    std::expected<utf::UtfTable, std::string> load_chunk_utf(
        uint64_t offset,
        uint64_t chunk_size,
        std::string_view expected_magic
    ) const;
    std::expected<void, std::string> populate_file_entries(uint64_t content_offset);
    void normalize_entry_path(CpkEntry& entry, const std::string& cpk_path) const;
    std::expected<std::span<const uint8_t>, std::string> packed_entry_span(const CpkEntry& entry) const;
    std::expected<void, std::string> write_entry_to_file(
        const CpkEntry& entry,
        const std::filesystem::path& output_path
    ) const;
    std::expected<void, std::string> rebuild_state(bool encrypt_utf_chunks);
    std::expected<std::vector<uint8_t>, std::string> save_impl(bool encrypt_utf_chunks);
    std::expected<std::vector<PreparedEntry>, std::string> prepare_entries_for_save();
    std::expected<std::vector<uint8_t>, std::string> build_archive(
        std::vector<PreparedEntry>& prepared_entries,
        bool encrypt_utf_chunks = false
    );
    std::expected<std::vector<uint8_t>, std::string> generate_toc(
        const std::vector<PreparedEntry>& prepared_entries,
        const std::vector<size_t>& toc_order,
        const std::vector<uint64_t>& entry_offsets
    ) const;
    std::vector<uint8_t> generate_itoc_mode0(
        const std::vector<PreparedEntry>& prepared_entries,
        const std::vector<size_t>& data_order
    ) const;
    std::vector<uint8_t> generate_itoc_mode2(
        const std::vector<PreparedEntry>& prepared_entries,
        const std::vector<size_t>& toc_order
    ) const;
    std::expected<std::vector<uint8_t>, std::string> generate_etoc() const;
    std::vector<uint8_t> generate_gtoc(
        const std::vector<PreparedEntry>& prepared_entries,
        uint64_t enabled_packed_size
    ) const;
    std::vector<uint8_t> generate_cpk_header(
        uint64_t enabled_packed_size,
        uint64_t enabled_data_size,
        uint64_t content_size,
        uint64_t toc_chunk_size,
        uint64_t itoc_chunk_offset,
        uint64_t itoc_chunk_size,
        uint64_t etoc_chunk_offset,
        uint64_t etoc_chunk_size,
        uint64_t gtoc_chunk_offset,
        uint64_t gtoc_chunk_size,
        uint64_t content_offset,
        uint64_t file_size,
        uint32_t toc_crc,
        uint32_t itoc_crc,
        uint32_t gtoc_crc
    ) const;
    std::vector<uint8_t> wrap_chunk(
        std::string_view magic,
        std::span<const uint8_t> table_data,
        bool encrypt_payload = false
    ) const;
    static void crypt_utf_payload(std::span<uint8_t> payload);

    static int compare_archive_paths(std::string_view lhs, std::string_view rhs);
};

class CpkReader {
public:
    CpkReader() = default;

    std::expected<void, std::string> load(const std::filesystem::path& path);
    std::expected<void, std::string> load(std::span<const uint8_t> data);
    std::expected<std::vector<uint8_t>, std::string> extract_to_memory(const CpkEntry& entry) const;
    std::expected<void, std::string> extract(const CpkEntry& entry, const std::filesystem::path& output_dir) const;
    std::expected<void, std::string> extract(
        const std::filesystem::path& output_dir,
        bool disambiguate_conflicts = true
    ) const;
    std::expected<void, std::string> extract_all(
        const std::filesystem::path& output_dir,
        bool disambiguate_conflicts = true
    ) const {
        return extract(output_dir, disambiguate_conflicts);
    }

    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_cpk.source_path(); }
    [[nodiscard]] CpkMode layout_mode() const noexcept { return m_cpk.layout_mode(); }
    [[nodiscard]] CpkMode mode() const noexcept { return m_cpk.mode(); }
    [[nodiscard]] CpkPreset preset() const noexcept { return m_cpk.preset(); }
    [[nodiscard]] bool has_declared_preset() const noexcept { return m_cpk.has_declared_preset(); }
    [[nodiscard]] CpkPreset declared_preset() const noexcept { return m_cpk.declared_preset(); }
    [[nodiscard]] uint64_t content_offset() const noexcept { return m_cpk.content_offset(); }
    [[nodiscard]] uint16_t alignment() const noexcept { return m_cpk.alignment(); }
    [[nodiscard]] const std::vector<CpkEntry>& files() const noexcept { return m_cpk.files(); }
    [[nodiscard]] bool has_toc() const noexcept { return m_cpk.has_toc(); }
    [[nodiscard]] bool has_itoc() const noexcept { return m_cpk.has_itoc(); }
    [[nodiscard]] bool has_gtoc() const noexcept { return m_cpk.has_gtoc(); }
    [[nodiscard]] bool has_etoc() const noexcept { return m_cpk.has_etoc(); }
    [[nodiscard]] const utf::UtfTable& cpk_header() const noexcept { return m_cpk.cpk_header(); }
    [[nodiscard]] const utf::UtfTable& toc() const noexcept { return m_cpk.toc(); }
    [[nodiscard]] const utf::UtfTable& itoc() const noexcept { return m_cpk.itoc(); }
    [[nodiscard]] const utf::UtfTable& gtoc() const noexcept { return m_cpk.gtoc(); }
    [[nodiscard]] const utf::UtfTable& etoc() const noexcept { return m_cpk.etoc(); }
    [[nodiscard]] const Cpk& archive() const noexcept { return m_cpk; }

private:
    Cpk m_cpk;
};

class CpkBuilder {
public:
    CpkBuilder() = default;

    void add_file(
        const std::filesystem::path& local_path,
        const std::string& cpk_path,
        bool compress = false,
        std::optional<uint32_t> id = std::nullopt
    );

    std::expected<std::vector<uint8_t>, std::string> build(const CpkBuilderOptions& options);
    std::expected<void, std::string> build_to_file(
        const std::filesystem::path& output_path,
        const CpkBuilderOptions& options
    );

private:
    Cpk m_cpk = Cpk::create();
};

} // namespace cricodecs::cpk
