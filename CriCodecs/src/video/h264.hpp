#pragma once

/**
 * @file h264.hpp
 * @brief H.264 Annex B access-unit traversal used by the USM builder.
 *
 * This is a structural parser, not a video decoder. It reads SPS dimensions and
 * timing, splits Annex B NAL units into access units, and marks IDR access
 * units so USM builder output can carry the same style of VIDEO_HDRINFO and
 * VIDEO_SEEKINFO metadata seen in samples produced by Medianoche.
 */

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../utilities/io.hpp"

namespace cricodecs::video {

struct H264SequenceParameterSet {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t profile_idc = 0;
    uint8_t level_idc = 0;
    uint32_t num_units_in_tick = 0;
    uint32_t time_scale = 0;
    bool fixed_frame_rate = false;
};

struct H264VideoFrame {
    uint32_t size = 0;
    uint32_t index = 0;
    bool is_keyframe = false;
    std::span<const uint8_t> data;
    std::span<const uint8_t> record_bytes;
};

struct H264Structure {
    uint32_t nal_units = 0;
    uint32_t valid_nal_headers = 0;
    uint32_t valid_slice_headers = 0;
    uint32_t emulation_prevention_bytes = 0;
    uint32_t ebsp_violations = 0;
};

[[nodiscard]] H264Structure inspect_h264_structure(std::span<const uint8_t> bytes) noexcept;

[[nodiscard]] std::expected<H264SequenceParameterSet, std::string> parse_h264_sequence_parameter_set(
    std::span<const uint8_t> bytes
);

class H264VideoReader {
public:
    H264VideoReader() = default;

    std::expected<void, std::string> open(const std::filesystem::path& path);
    std::expected<void, std::string> open(std::span<const uint8_t> bytes);

    [[nodiscard]] const H264SequenceParameterSet& sequence_parameter_set() const noexcept { return m_sps; }
    [[nodiscard]] uint32_t frame_count() const noexcept { return static_cast<uint32_t>(m_frames.size()); }
    [[nodiscard]] std::span<const uint8_t> data() const noexcept { return m_reader.data(); }
    [[nodiscard]] std::pair<uint32_t, uint32_t> frame_rate() const noexcept;

    [[nodiscard]] bool has_frames() const noexcept { return m_current_frame < m_frames.size(); }
    std::expected<H264VideoFrame, std::string> read_next_frame();

private:
    struct FrameRange {
        size_t offset = 0;
        size_t size = 0;
        bool is_keyframe = false;
    };

    std::expected<void, std::string> parse_loaded_stream(std::string_view source_name);
    [[nodiscard]] static std::vector<FrameRange> split_frames(std::span<const uint8_t> bytes);

    io::reader m_reader;
    H264SequenceParameterSet m_sps{};
    std::vector<FrameRange> m_frames;
    uint32_t m_current_frame = 0;
};

} // namespace cricodecs::video
