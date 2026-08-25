#pragma once
/**
 * @file acx_container.hpp
 * @brief ACX - legacy CRI multi-stream archive container
 *
 * Ported from vgmstream's ACX loader. An ACX file is a simple big-endian
 * table of subfile offsets and sizes that commonly contains ADX streams and
 * occasionally Ogg Vorbis streams.
 */

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "../utilities/io_reader.hpp"

namespace cricodecs::acx {

enum class AcxEntryType {
    unknown,
    adx,
    ogg,
};

[[nodiscard]] constexpr const char* entry_extension(AcxEntryType type) noexcept {
    switch (type) {
        case AcxEntryType::adx: return ".adx";
        case AcxEntryType::ogg: return ".ogg";
        default: return ".bin";
    }
}

struct AcxEntry {
    uint32_t index = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
    AcxEntryType type = AcxEntryType::unknown;

    [[nodiscard]] std::filesystem::path suggested_path(bool include_index_prefix = true) const;
};

class AcxContainer {
public:
    AcxContainer() = default;

    [[nodiscard]] static std::expected<AcxContainer, std::string> load(std::span<const uint8_t> data);
    [[nodiscard]] static std::expected<AcxContainer, std::string> load(
        std::span<const uint8_t> data,
        io::SourceView::Owner owner
    );
    [[nodiscard]] static std::expected<AcxContainer, std::string> load(const std::filesystem::path& path);

    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }
    [[nodiscard]] uint32_t entry_count() const noexcept { return static_cast<uint32_t>(m_entries.size()); }
    [[nodiscard]] uint32_t table_size() const noexcept {
        return 0x08u + static_cast<uint32_t>(m_entries.size()) * 0x08u;
    }
    [[nodiscard]] uint32_t type_count(AcxEntryType type) const noexcept {
        uint32_t count = 0;
        for (const auto& entry : m_entries) {
            if (entry.type == type) {
                ++count;
            }
        }
        return count;
    }
    [[nodiscard]] std::optional<uint32_t> first_payload_offset() const noexcept {
        if (m_entries.empty()) {
            return std::nullopt;
        }
        uint32_t first = m_entries.front().offset;
        for (const auto& entry : m_entries) {
            if (entry.offset < first) {
                first = entry.offset;
            }
        }
        return first;
    }
    [[nodiscard]] std::optional<uint64_t> payload_end_offset() const noexcept {
        if (m_entries.empty()) {
            return std::nullopt;
        }
        uint64_t end = 0;
        for (const auto& entry : m_entries) {
            const uint64_t entry_end = static_cast<uint64_t>(entry.offset) + entry.size;
            if (entry_end > end) {
                end = entry_end;
            }
        }
        return end;
    }
    [[nodiscard]] const std::vector<AcxEntry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] const AcxEntry& entry(uint32_t index) const { return m_entries[index]; }

    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> file_data(uint32_t index) const;
    [[nodiscard]] std::expected<void, std::string> extract_file(
        uint32_t index,
        const std::filesystem::path& output_path
    ) const {
        return export_stream(index, output_path);
    }
    [[nodiscard]] std::expected<void, std::string> extract(const std::filesystem::path& output_dir) const {
        return export_all(output_dir);
    }
    [[nodiscard]] std::expected<void, std::string> export_stream(uint32_t index, const std::filesystem::path& output_path) const;
    [[nodiscard]] std::expected<void, std::string> export_all(const std::filesystem::path& output_dir) const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> rebuild() const;
    [[nodiscard]] std::expected<void, std::string> save_to_file(const std::filesystem::path& output_path) const;
    [[nodiscard]] std::expected<void, std::string> set_file_data(uint32_t index, std::span<const uint8_t> data);
    [[nodiscard]] std::expected<void, std::string> add_file(std::span<const uint8_t> data);
    [[nodiscard]] std::expected<void, std::string> remove_file(uint32_t index);
    [[nodiscard]] std::expected<void, std::string> move_file(uint32_t from_index, uint32_t to_index);

private:
    io::SourceView m_source;
    io::reader m_reader;
    std::vector<uint8_t> m_owned_source;
    std::filesystem::path m_source_path;
    std::vector<AcxEntry> m_entries;

    [[nodiscard]] std::expected<void, std::string> parse();
    [[nodiscard]] std::expected<std::vector<std::vector<uint8_t>>, std::string>
        copy_payloads() const;
    [[nodiscard]] std::expected<void, std::string> replace_payloads(std::vector<std::vector<uint8_t>> payloads);
};

} // namespace cricodecs::acx
