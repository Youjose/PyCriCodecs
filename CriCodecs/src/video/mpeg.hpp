#pragma once

/**
 * @file mpeg.hpp
 * @brief MPEG elementary-stream traversal used by USM/SFD tooling.
 *
 * The API retrieves MPEG frames. It finds the sequence header,
 * splits elementary streams into picture records, and exposes
 * frame-rate/keyframe data needed to rebuild USM VIDEO_HDRINFO and
 * VIDEO_SEEKINFO tables observed in samples.
 */

#include <cstdint>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "../utilities/io.hpp"

namespace cricodecs::video {

enum class MpegVideoType : uint8_t {
    unknown = 0,
    mpeg1,
    mpeg2,
};

struct MpegVideoSequenceHeader {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t aspect_ratio_code = 0;
    uint8_t frame_rate_code = 0;
    uint32_t bit_rate_value = 0;
};

struct MpegVideoFrame {
    uint32_t size = 0;
    uint32_t index = 0;
    bool is_keyframe = false;
    std::span<const uint8_t> data;
    std::span<const uint8_t> record_bytes;
};

struct MpegStructure {
    uint32_t start_codes = 0;
    uint32_t valid_start_codes = 0;
    uint32_t pictures = 0;
    uint32_t slices = 0;
    uint32_t violations = 0;
};

struct MpegFrameRange {
    size_t offset = 0;
    size_t size = 0;
    bool is_keyframe = false;
};

[[nodiscard]] MpegStructure inspect_mpeg_structure(std::span<const uint8_t> bytes) noexcept;

[[nodiscard]] constexpr std::pair<uint32_t, uint32_t> mpeg_frame_rate_ratio(uint8_t frame_rate_code) noexcept {
    switch (frame_rate_code) {
        case 1: return {24000, 1001};
        case 2: return {24, 1};
        case 3: return {25, 1};
        case 4: return {30000, 1001};
        case 5: return {30, 1};
        case 6: return {50, 1};
        case 7: return {60000, 1001};
        case 8: return {60, 1};
        default: return {30000, 1001};
    }
}

[[nodiscard]] std::expected<MpegVideoSequenceHeader, std::string> parse_mpeg_sequence_header(
    std::span<const uint8_t> bytes
);

[[nodiscard]] MpegVideoType detect_mpeg_video_type(std::span<const uint8_t> bytes) noexcept;

class MpegVideoReader {
public:
    MpegVideoReader() = default;

    std::expected<void, std::string> open(const std::filesystem::path& path);
    std::expected<void, std::string> open(std::span<const uint8_t> bytes);

    [[nodiscard]] const MpegVideoSequenceHeader& sequence_header() const noexcept { return m_sequence_header; }
    [[nodiscard]] MpegVideoType video_type() const noexcept { return m_video_type; }
    [[nodiscard]] std::pair<uint32_t, uint32_t> frame_rate() const noexcept {
        return mpeg_frame_rate_ratio(m_sequence_header.frame_rate_code);
    }
    [[nodiscard]] uint32_t frame_count() const noexcept { return static_cast<uint32_t>(m_frames.size()); }
    [[nodiscard]] std::span<const uint8_t> data() const noexcept { return m_reader.data(); }

    [[nodiscard]] bool has_frames() const noexcept { return m_current_frame < m_frames.size(); }
    std::expected<MpegVideoFrame, std::string> read_next_frame();

private:
    std::expected<void, std::string> parse_loaded_stream(std::string_view source_name);

    io::reader m_reader;
    MpegVideoSequenceHeader m_sequence_header{};
    MpegVideoType m_video_type = MpegVideoType::unknown;
    std::vector<MpegFrameRange> m_frames;
    uint32_t m_current_frame = 0;
};

} // namespace cricodecs::video
