#pragma once

/**
 * @file ivf.hpp
 * @brief IVF/VP9 frame traversal used by the USM builder.
 *
 * This reader preserves the raw IVF file header and per-frame records so USM
 * muxing can round-trip VP9 streams in their original order while exposing keyframe
 * markers needed for generated VIDEO_SEEKINFO metadata.
 */

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>

#include "../utilities/io.hpp"

namespace cricodecs::video {

struct IvfHeader {
    uint32_t magic = 0; // DKIF
    uint16_t version = 0;
    uint16_t header_size = 0;
    uint32_t fourcc = 0; // VP90
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t rate = 0;
    uint32_t scale = 0;
    uint32_t num_frames = 0;
    uint32_t unused = 0;
};

struct IvfFrame {
    uint32_t size = 0;
    uint64_t timestamp = 0;
    bool is_keyframe = false;
    std::span<const uint8_t> data;
    std::span<const uint8_t> record_bytes;
};

class IvfReader {
public:
    IvfReader() = default;

    std::expected<void, std::string> open(const std::filesystem::path& path);
    std::expected<void, std::string> open(std::span<const uint8_t> bytes);

    [[nodiscard]] const IvfHeader& get_header() const noexcept { return m_header; }
    [[nodiscard]] std::span<const uint8_t> get_raw_header() const noexcept {
        return m_reader.size() >= 32 ? m_reader.data().first(32) : std::span<const uint8_t>{};
    }
    [[nodiscard]] bool has_frames() const;
    std::expected<IvfFrame, std::string> read_next_frame();

private:
    std::expected<void, std::string> parse_header();

    io::reader m_reader;
    IvfHeader m_header;
};

/// Accumulates an IVF presentation timeline without assuming one PTS unit per frame.
class IvfTimeline {
public:
    [[nodiscard]] std::expected<void, std::string> observe(uint64_t timestamp);

    [[nodiscard]] uint32_t frame_count() const noexcept { return m_frame_count; }
    [[nodiscard]] uint64_t first_timestamp() const noexcept { return m_first_timestamp; }
    [[nodiscard]] uint64_t last_timestamp() const noexcept { return m_last_timestamp; }

    /// Convert an observed timestamp to another clock, relative to the first frame.
    [[nodiscard]] std::expected<uint32_t, std::string> time_in(
        uint64_t timestamp,
        uint32_t rate,
        uint32_t scale,
        uint32_t output_rate) const;
    /// Average presentation rate expressed as frames per second multiplied by 1000.
    [[nodiscard]] uint32_t frame_rate_milli(uint32_t rate, uint32_t scale) const noexcept;
    /// Presentation duration in milliseconds, including one estimated final-frame interval.
    [[nodiscard]] uint64_t duration_milliseconds(uint32_t rate, uint32_t scale) const noexcept;
    [[nodiscard]] long double duration_seconds(uint32_t rate, uint32_t scale) const noexcept;

private:
    uint64_t m_first_timestamp = 0;
    uint64_t m_last_timestamp = 0;
    uint32_t m_frame_count = 0;
};

/// Validate a VP9 frame, including a present superframe index and all indexed subframe headers.
[[nodiscard]] bool is_valid_vp9_frame(std::span<const uint8_t> frame_bytes) noexcept;
/// Return the number of structurally valid VP9 subframes, or zero for an invalid frame/index.
[[nodiscard]] size_t vp9_subframe_count(std::span<const uint8_t> frame_bytes) noexcept;

} // namespace cricodecs::video
