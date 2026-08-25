#pragma once
/**
 * @file csb_container.hpp
 * @brief CSB (Cue Sheet Binary) - legacy CRI cue/archive container
 *
 * Ported from vgmstream's CSB loader. A CSB is a UTF table whose
 * SOUND_ELEMENT sub-table contains embedded audio wrappers such as AAX,
 * HCA, AHX, and ADPCM_WII.
 */

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "../utf/utf_table.hpp"
#include "../utilities/text_encoding.hpp"

namespace cricodecs::csb {

// For CSB extraction the practical format mapping is stable:
// - fmt 0: standalone AAX UTF wrapper
// - fmt 2: AHX payload wrapped in an AHX UTF table
// - fmt 4: Nintendo DSP payload wrapped in an ADPCM_WII UTF table
// - fmt 6: HCA payload wrapped in an HCA UTF table
//
// Wrapper table names are still parsed and validated when extracting, but the
// exported extension is determined from the CSB format field.

[[nodiscard]] constexpr const char* stream_file_extension(uint8_t format) noexcept {
    switch (format) {
        case 0: return ".aax";
        case 2: return ".ahx";
        case 4: return ".dsp";
        case 6: return ".hca";
        default: return ".bin";
    }
}

struct CsbSection {
    uint32_t row_index = 0;
    std::string name;
    uint8_t table_type = 0;
    uint32_t data_size = 0;
};

struct CsbStreamInfo {
    uint32_t row_index = 0;
    std::string name;
    std::string name_raw;
    uint8_t format = 0;
    std::string wrapper_table_name;
    uint8_t channels = 0;
    uint32_t sample_rate = 0;
    uint32_t sample_count = 0;
    bool streamed = false;
    uint32_t wrapper_size = 0;

    [[nodiscard]] std::filesystem::path suggested_path() const;
};

struct CsbBuildEntry {
    std::filesystem::path source_path;
    std::filesystem::path archive_path;
};

class CsbContainer {
public:
    CsbContainer() = default;

    [[nodiscard]] static std::expected<CsbContainer, std::string> load(
        const std::filesystem::path& path,
        const text::EncodingOptions& encoding = {}
    );
    [[nodiscard]] static std::expected<CsbContainer, std::string> load(
        std::span<const uint8_t> data,
        const text::EncodingOptions& encoding = {}
    );
    [[nodiscard]] static std::expected<CsbContainer, std::string> load(
        std::vector<uint8_t>&& data,
        const text::EncodingOptions& encoding = {}
    );
    [[nodiscard]] static std::expected<std::vector<uint8_t>, std::string> build(
        std::span<const CsbBuildEntry> entries,
        const text::EncodingOptions& encoding = {}
    );
    [[nodiscard]] static std::expected<std::vector<uint8_t>, std::string> build_from_directory(
        const std::filesystem::path& input_dir,
        const text::EncodingOptions& encoding = {}
    );
    [[nodiscard]] static std::expected<void, std::string> build_to_file(
        std::span<const CsbBuildEntry> entries,
        const std::filesystem::path& output_path,
        const text::EncodingOptions& encoding = {}
    );
    [[nodiscard]] static std::expected<void, std::string> build_to_file(
        const std::filesystem::path& input_dir,
        const std::filesystem::path& output_path,
        const text::EncodingOptions& encoding = {}
    );

    [[nodiscard]] const utf::UtfTable& header_table() const noexcept { return m_header; }
    [[nodiscard]] const utf::UtfTable& sound_element_table() const noexcept { return m_sound_element; }
    [[nodiscard]] std::string_view name() const noexcept { return m_header.table_name(); }
    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }

    [[nodiscard]] const std::vector<CsbSection>& sections() const noexcept { return m_sections; }
    [[nodiscard]] const std::vector<CsbStreamInfo>& elements() const noexcept { return m_elements; }

    [[nodiscard]] uint32_t section_count() const noexcept { return static_cast<uint32_t>(m_sections.size()); }
    [[nodiscard]] uint32_t element_count() const noexcept { return static_cast<uint32_t>(m_elements.size()); }
    [[nodiscard]] uint32_t stream_count() const noexcept { return static_cast<uint32_t>(m_embedded_indices.size()); }

    [[nodiscard]] const CsbSection& section(uint32_t index) const { return m_sections[index]; }
    [[nodiscard]] const CsbStreamInfo& element(uint32_t index) const { return m_elements[index]; }
    [[nodiscard]] const CsbStreamInfo& stream(uint32_t index) const { return m_elements[m_embedded_indices[index]]; }

    [[nodiscard]] std::expected<utf::UtfTable, std::string> section_table(uint32_t index) const;
    [[nodiscard]] std::expected<utf::UtfTable, std::string> wrapper_table(uint32_t index) const;
    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> wrapper_data(uint32_t index) const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> stream_data(uint32_t index) const;
    [[nodiscard]] std::expected<void, std::string> extract_file(
        uint32_t index,
        const std::filesystem::path& output_path
    ) const {
        return export_stream(index, output_path);
    }
    [[nodiscard]] std::expected<void, std::string> extract(const std::filesystem::path& output_dir) const {
        return export_all(output_dir);
    }
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> save() const;
    [[nodiscard]] std::expected<void, std::string> save_to_file(const std::filesystem::path& output_path) const;
    [[nodiscard]] std::expected<void, std::string> export_stream(uint32_t index, const std::filesystem::path& output_path) const;
    [[nodiscard]] std::expected<void, std::string> export_all(const std::filesystem::path& output_dir) const;

    [[nodiscard]] std::expected<void, std::string> add_file(
        std::span<const uint8_t> bytes,
        const std::filesystem::path& archive_path
    );
    [[nodiscard]] std::expected<void, std::string> replace_file(uint32_t index, std::span<const uint8_t> bytes);
    [[nodiscard]] std::expected<void, std::string> remove_file(uint32_t index);
    [[nodiscard]] std::expected<void, std::string> move_file(uint32_t from_index, uint32_t to_index);
    [[nodiscard]] std::expected<void, std::string> rename_file(
        uint32_t index,
        const std::filesystem::path& archive_path
    );
    [[nodiscard]] std::expected<void, std::string> set_streamed(uint32_t index, bool streamed);
    [[nodiscard]] std::expected<void, std::string> set_element_streamed(uint32_t index, bool streamed);

private:
    std::span<const uint8_t> m_source;
    std::vector<uint8_t> m_owned_source;
    std::filesystem::path m_source_path;
    utf::UtfTable m_header;
    utf::UtfTable m_sound_element;
    text::EncodingOptions m_encoding;

    std::vector<CsbSection> m_sections;
    std::vector<CsbStreamInfo> m_elements;
    std::vector<uint32_t> m_embedded_indices;

    [[nodiscard]] std::expected<void, std::string> parse();
    [[nodiscard]] std::expected<void, std::string> parse_sections();
    [[nodiscard]] std::expected<void, std::string> parse_sound_elements();

    [[nodiscard]] std::expected<void, std::string> replace_sound_element(utf::UtfTable sound_element);
};

} // namespace cricodecs::csb
