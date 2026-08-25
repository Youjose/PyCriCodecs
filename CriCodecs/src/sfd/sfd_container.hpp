#pragma once
/**
 * @file sfd_container.hpp
 * @brief SFD / SofDec 1 reader and builder surface
 *
 * SofDec 1 uses an MPEG program-stream-style container with CRI-specific
 * metadata in private stream 2 (`0xBF`) and usually carries one MPEG video
 * stream plus one CRI audio stream such as ADX. The current builder focuses on
 * the practical fixed-pack mux path recovered from the official tools:
 * 2048-byte packs, one video stream, and optional one audio stream. The public
 * builder surface keeps generic `SofdecStream` naming while compatibility
 * aliases preserve older tool-specific spellings.
 */

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../utilities/io.hpp"

namespace cricodecs::sfd {

enum class SfdStreamType {
    audio,
    video,
    private_data,
};

enum class SfdAudioType {
    unknown,
    adx,
    aix,
    ac3,
};

enum class SfdVideoType {
    unknown,
    mpeg1,
    mpeg2,
};

enum class SfdHeaderVariant {
    unknown,
    sofdec_stream,
    sofdec_stream2,
};

enum class SfdBuildProfile {
    sofdec_stream_standard_fixed_2048,
    sofdec_stream_fixed_2048 = sofdec_stream_standard_fixed_2048,
    sofdec_stream_sfdmuxg_fixed_2048 = sofdec_stream_standard_fixed_2048,
    sofdec_stream2_fixed_2048_v23249,
    sofdec_stream2_fixed_2048_v23310,
    sofdec_stream2_craft = sofdec_stream2_fixed_2048_v23249,
    sofdec_stream2_medianoche = sofdec_stream2_fixed_2048_v23310,
};

[[nodiscard]] constexpr const char* stream_extension(SfdAudioType type) noexcept {
    switch (type) {
        case SfdAudioType::adx: return ".adx";
        case SfdAudioType::aix: return ".aix";
        case SfdAudioType::ac3: return ".ac3";
        default: return ".bin";
    }
}

struct SfdVideoSequenceHeader {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t aspect_ratio_code = 0;
    uint8_t frame_rate_code = 0;
    uint32_t bit_rate_value = 0;
};

struct SfdChunkSpan {
    uint64_t source_offset = 0;
    uint32_t size = 0;
};

struct SfdElementRecord {
    uint8_t stream_id = 0;
    uint8_t source_type = 0;
    std::string short_name;
    std::string timestamp;
    std::array<uint8_t, 6> detail_bytes{};
    std::array<uint8_t, 32> footer_bytes{};
    std::optional<uint16_t> picture_rate;
    std::optional<uint16_t> width;
    std::optional<uint16_t> height;
    std::optional<uint8_t> frame_rate_code;
    std::optional<uint8_t> audio_channels;
    std::optional<uint32_t> audio_sample_rate;
};

struct SfdHeaderSummary {
    SfdHeaderVariant variant = SfdHeaderVariant::unknown;
    std::string header_label;
    std::array<uint8_t, 4> version_tag_bytes{};
    uint8_t version_tag_size = 0;
    uint32_t pack_size = 0;
    bool variable_pack = false;
    uint16_t min_header_packet_count = 0;
    uint32_t reserved_header_size = 0;
    uint8_t element_count = 0;
    uint8_t audio_count = 0;
    uint8_t video_count = 0;
    uint8_t private_count = 0;
    uint64_t bitrate_bytes_per_second = 0;
    std::string short_output_name;
    std::string output_timestamp;
    std::string output_name;
    std::string builder_version;
    std::vector<SfdElementRecord> element_records;
};

struct SfdStream {
    uint32_t index = 0;
    uint32_t type_index = 0;
    SfdStreamType type = SfdStreamType::audio;
    uint8_t stream_id = 0;
    SfdAudioType audio_type = SfdAudioType::unknown;
    SfdVideoType video_type = SfdVideoType::unknown;
    std::string source_name;
    std::optional<SfdVideoSequenceHeader> video_header;
    std::optional<SfdElementRecord> element_record;
    uint64_t extracted_size = 0;
    uint32_t packet_count = 0;
    std::vector<SfdChunkSpan> chunks;

    [[nodiscard]] std::filesystem::path suggested_path(bool include_index_prefix = true) const;
};

class SfdContainer {
public:
    SfdContainer() = default;

    [[nodiscard]] static std::expected<SfdContainer, std::string> load(const std::filesystem::path& path);
    [[nodiscard]] static std::expected<SfdContainer, std::string> load(std::span<const uint8_t> data);
    [[nodiscard]] static std::expected<SfdContainer, std::string> load(std::vector<uint8_t>&& data);

    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }
    [[nodiscard]] const std::vector<SfdStream>& streams() const noexcept { return m_streams; }
    [[nodiscard]] const std::optional<SfdHeaderSummary>& header_summary() const noexcept { return m_header_summary; }
    [[nodiscard]] uint32_t stream_count() const noexcept { return static_cast<uint32_t>(m_streams.size()); }
    [[nodiscard]] const SfdStream& stream(uint32_t index) const { return m_streams[index]; }
    [[nodiscard]] const SfdStream* find_stream_by_id(uint8_t stream_id) const noexcept;

    [[nodiscard]] std::expected<std::map<std::string, std::vector<uint8_t>>, std::string> demux(
        bool include_index_prefix = true
    ) const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> extract_stream(uint32_t index) const;
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

private:
    io::SourceView m_source;
    std::filesystem::path m_source_path;
    std::vector<SfdStream> m_streams;
    std::optional<SfdHeaderSummary> m_header_summary;

    [[nodiscard]] std::expected<void, std::string> parse();
};

struct SfdBuildInput {
    std::filesystem::path video_path;
    std::optional<std::filesystem::path> audio_path;
    std::string video_source_name;
    std::string audio_source_name;
    std::string video_stream_name;
    std::string audio_stream_name;
    std::string output_name;
    std::optional<SfdBuildProfile> build_profile;
    std::string header_builder_version;
    std::optional<SfdBuildProfile> mux_profile;
    std::string header_builder_version_override;
};

class SfdBuilder {
public:
    SfdBuilder() = default;

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build(const SfdBuildInput& input);
    [[nodiscard]] std::expected<void, std::string> build_to_file(
        const std::filesystem::path& output_path,
        const SfdBuildInput& input
    );
};

} // namespace cricodecs::sfd
