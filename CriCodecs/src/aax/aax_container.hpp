#pragma once
/**
 * @file aax_container.hpp
 * @brief AAX - CRI UTF wrapper around segmented ADX payloads
 *
 * Ported from vgmstream's AAX loader. An AAX file is a small UTF table whose
 * rows contain one or more ADX segments plus optional `lpflg` loop markers.
 */

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "../utf/utf_table.hpp"

namespace cricodecs::aax {

struct AaxSegmentInfo {
    uint32_t row_index = 0;
    uint32_t data_size = 0;
    uint32_t sample_count = 0;
    bool loop_segment = false;
};

struct AaxBuildEntry {
    std::vector<uint8_t> adx_data;
    bool loop_segment = false;
};

class AaxContainer {
public:
    AaxContainer() = default;

    [[nodiscard]] static std::expected<AaxContainer, std::string> load(const std::filesystem::path& path);
    [[nodiscard]] static std::expected<AaxContainer, std::string> load(std::span<const uint8_t> data);
    [[nodiscard]] static std::expected<std::vector<uint8_t>, std::string> build(std::span<const AaxBuildEntry> entries);
    [[nodiscard]] static std::expected<void, std::string> build_to_file(
        std::span<const AaxBuildEntry> entries,
        const std::filesystem::path& output_path
    );

    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }
    [[nodiscard]] const utf::UtfTable& table() const noexcept { return m_table; }
    [[nodiscard]] std::string_view name() const noexcept { return m_table.table_name(); }

    [[nodiscard]] uint32_t segment_count() const noexcept { return static_cast<uint32_t>(m_segments.size()); }
    [[nodiscard]] const std::vector<AaxSegmentInfo>& segments() const noexcept { return m_segments; }
    [[nodiscard]] const AaxSegmentInfo& segment(uint32_t index) const { return m_segments[index]; }

    [[nodiscard]] uint8_t channels() const noexcept { return m_channels; }
    [[nodiscard]] uint32_t sample_rate() const noexcept { return m_sample_rate; }
    [[nodiscard]] uint32_t sample_count() const noexcept;
    [[nodiscard]] bool has_loop_segments() const noexcept;

    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> segment_data(uint32_t index) const;
    [[nodiscard]] std::expected<void, std::string> extract_file(
        uint32_t index,
        const std::filesystem::path& output_path
    ) const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> adx_data() const;
    [[nodiscard]] std::expected<void, std::string> extract(const std::filesystem::path& output_path) const {
        return export_adx(output_path);
    }
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> save() const;
    [[nodiscard]] std::expected<void, std::string> save_to_file(const std::filesystem::path& output_path) const;
    [[nodiscard]] std::expected<void, std::string> export_adx(const std::filesystem::path& output_path) const;

    [[nodiscard]] std::expected<void, std::string> add_segment(
        std::span<const uint8_t> adx_data,
        bool loop_segment = false
    );
    [[nodiscard]] std::expected<void, std::string> replace_segment(
        uint32_t index,
        std::span<const uint8_t> adx_data
    );
    [[nodiscard]] std::expected<void, std::string> remove_segment(uint32_t index);
    [[nodiscard]] std::expected<void, std::string> move_segment(uint32_t from_index, uint32_t to_index);
    [[nodiscard]] std::expected<void, std::string> set_loop_segment(uint32_t index, bool loop_segment);

private:
    std::filesystem::path m_source_path;
    utf::UtfTable m_table;
    std::vector<AaxSegmentInfo> m_segments;
    mutable std::vector<std::vector<uint8_t>> m_projected_segments;

    uint8_t m_channels = 0;
    uint32_t m_sample_rate = 0;

    [[nodiscard]] static std::expected<AaxContainer, std::string> load_owned(
        std::vector<uint8_t> data,
        std::filesystem::path source_path = {}
    );
    [[nodiscard]] std::expected<void, std::string> parse();
    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> raw_segment_data(uint32_t index) const;
    [[nodiscard]] std::expected<std::vector<AaxBuildEntry>, std::string> build_entries() const;
    [[nodiscard]] std::expected<void, std::string> replace_entries(std::vector<AaxBuildEntry> entries);
};

} // namespace cricodecs::aax
