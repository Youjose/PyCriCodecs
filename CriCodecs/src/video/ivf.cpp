/**
 * @file ivf.cpp
 * @brief IVF/VP9 parser for USM frame packaging.
 */

#include "ivf.hpp"

#include <cmath>
#include <limits>

namespace cricodecs::video {

namespace {

constexpr io::FourCC IvfMagic{"DKIF"};

struct Vp9HeaderProbe {
    bool valid = false;
    bool keyframe = false;
};

struct IvfFrameHeader {
    uint32_t size;
    uint32_t timestamp_low;
    uint32_t timestamp_high;

    [[nodiscard]] uint64_t timestamp() const noexcept {
        return timestamp_low | (static_cast<uint64_t>(timestamp_high) << 32u);
    }
};

Vp9HeaderProbe probe_vp9_header(std::span<const uint8_t> frame_bytes) noexcept {
    if (frame_bytes.empty()) {
        return {};
    }

    io::bit_reader reader(frame_bytes);
    if (reader.remaining() < 6u) {
        return {};
    }

    const auto frame_marker = reader.read(2);
    if (frame_marker != 0x02) {
        return {};
    }

    const auto profile_low = reader.read(1);
    const auto profile_high = reader.read(1);
    const auto profile = profile_low | (profile_high << 1u);
    if (profile == 3u) {
        if (reader.remaining() < 1u) {
            return {};
        }
        const auto reserved_zero = reader.read(1);
        if (reserved_zero != 0u) {
            return {};
        }
    }

    if (reader.remaining() < 2u) {
        return {};
    }

    const auto show_existing_frame = reader.read(1);
    if (show_existing_frame != 0u) {
        return Vp9HeaderProbe{.valid = reader.remaining() >= 3u, .keyframe = false};
    }

    const auto frame_type = reader.read(1);
    return Vp9HeaderProbe{.valid = true, .keyframe = frame_type == 0u};
}

bool is_vp9_keyframe(std::span<const uint8_t> frame_bytes) noexcept {
    return probe_vp9_header(frame_bytes).keyframe;
}

} // namespace

std::expected<void, std::string> IvfReader::open(const std::filesystem::path& path) {
    if (auto result = m_reader.open(path); !result) {
        return std::unexpected("IVF load failed: could not open input file: " + path.string());
    }
    return parse_header();
}

std::expected<void, std::string> IvfReader::open(std::span<const uint8_t> bytes) {
    if (auto result = m_reader.open(bytes); !result) {
        return std::unexpected("IVF load failed: could not open memory buffer");
    }

    return parse_header();
}

std::expected<void, std::string> IvfReader::parse_header() {
    if (m_reader.size() < 32) {
        return std::unexpected("IVF parse failed: file is too small for header");
    }

    m_reader.seek(0);
    m_header = m_reader.read_le<IvfHeader>();
    if (m_header.magic != IvfMagic.le_value()) {
        return std::unexpected("IVF parse failed: invalid magic");
    }
    return {};
}

bool IvfReader::has_frames() const {
    return m_reader.size() > 0 && m_reader.remaining() >= sizeof(IvfFrameHeader);
}

std::expected<IvfFrame, std::string> IvfReader::read_next_frame() {
    if (m_reader.remaining() < sizeof(IvfFrameHeader)) {
        return std::unexpected("IVF frame read failed: unexpected end of file before frame header");
    }

    const auto frame_offset = m_reader.tell();
    const auto header = m_reader.read_le<IvfFrameHeader>();

    IvfFrame frame;
    frame.size = header.size;
    frame.timestamp = header.timestamp();

    if (m_reader.remaining() < header.size) {
        return std::unexpected("IVF frame read failed: frame payload is truncated");
    }

    auto frame_data = m_reader.read_bytes(header.size);
    frame.data = frame_data;
    frame.is_keyframe = is_vp9_keyframe(frame.data);
    frame.record_bytes = m_reader.subspan(frame_offset, sizeof(IvfFrameHeader) + header.size);

    return frame;
}

std::expected<void, std::string> IvfTimeline::observe(uint64_t timestamp) {
    if (m_frame_count == 0) {
        m_first_timestamp = timestamp;
        m_last_timestamp = timestamp;
        m_frame_count = 1;
        return {};
    }
    if (timestamp <= m_last_timestamp) {
        return std::unexpected("IVF timeline contains duplicate or decreasing frame timestamps");
    }
    m_last_timestamp = timestamp;
    ++m_frame_count;
    return {};
}

std::expected<uint32_t, std::string> IvfTimeline::time_in(
    uint64_t timestamp,
    uint32_t rate,
    uint32_t scale,
    uint32_t output_rate) const {
    if (m_frame_count == 0 || timestamp < m_first_timestamp) {
        return std::unexpected("IVF timestamp conversion requires an observed frame timestamp");
    }
    if (rate == 0 || scale == 0 || output_rate == 0) {
        return std::unexpected("IVF timestamp conversion requires non-zero clock rates");
    }

    const long double converted =
        static_cast<long double>(timestamp - m_first_timestamp) *
        static_cast<long double>(scale) * static_cast<long double>(output_rate) /
        static_cast<long double>(rate);
    if (!std::isfinite(converted) ||
        converted > static_cast<long double>((std::numeric_limits<uint32_t>::max)())) {
        return std::unexpected("IVF timestamp conversion exceeds the USM time range");
    }
    return static_cast<uint32_t>(std::llround(converted));
}

uint32_t IvfTimeline::frame_rate_milli(uint32_t rate, uint32_t scale) const noexcept {
    if (m_frame_count == 0 || rate == 0 || scale == 0) {
        return 0;
    }

    long double frames_per_second =
        static_cast<long double>(rate) / static_cast<long double>(scale);
    if (m_frame_count > 1) {
        const auto elapsed = m_last_timestamp - m_first_timestamp;
        if (elapsed == 0) {
            return 0;
        }
        frames_per_second =
            static_cast<long double>(m_frame_count - 1u) * static_cast<long double>(rate) /
            (static_cast<long double>(elapsed) * static_cast<long double>(scale));
    }

    const long double result = frames_per_second * 1000.0L;
    if (!std::isfinite(result) || result <= 0.0L ||
        result > static_cast<long double>((std::numeric_limits<uint32_t>::max)())) {
        return 0;
    }
    return static_cast<uint32_t>(std::llround(result));
}

long double IvfTimeline::duration_seconds(uint32_t rate, uint32_t scale) const noexcept {
    if (m_frame_count == 0 || rate == 0 || scale == 0) {
        return 0.0L;
    }
    if (m_frame_count == 1) {
        return static_cast<long double>(scale) / static_cast<long double>(rate);
    }

    const long double elapsed = static_cast<long double>(m_last_timestamp - m_first_timestamp);
    const long double average_interval = elapsed / static_cast<long double>(m_frame_count - 1u);
    return (elapsed + average_interval) * static_cast<long double>(scale) /
        static_cast<long double>(rate);
}

uint64_t IvfTimeline::duration_milliseconds(uint32_t rate, uint32_t scale) const noexcept {
    const long double result = duration_seconds(rate, scale) * 1000.0L;
    if (!std::isfinite(result) || result <= 0.0L ||
        result > static_cast<long double>((std::numeric_limits<long long>::max)())) {
        return 0;
    }
    return static_cast<uint64_t>(std::llround(result));
}

size_t vp9_subframe_count(std::span<const uint8_t> frame_bytes) noexcept {
    if (!probe_vp9_header(frame_bytes).valid) {
        return 0;
    }

    const uint8_t marker = frame_bytes.back();
    if ((marker & 0xE0u) != 0xC0u) {
        return 1;
    }

    const size_t frame_count = static_cast<size_t>(marker & 0x07u) + 1u;
    const size_t magnitude = static_cast<size_t>((marker >> 3u) & 0x03u) + 1u;
    const size_t index_size = 2u + frame_count * magnitude;
    if (index_size > frame_bytes.size()) {
        return 0;
    }

    const size_t index_offset = frame_bytes.size() - index_size;
    if (frame_bytes[index_offset] != marker) {
        return 0;
    }

    size_t payload_offset = 0;
    size_t size_offset = index_offset + 1u;
    for (size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        size_t subframe_size = 0;
        for (size_t byte_index = 0; byte_index < magnitude; ++byte_index) {
            subframe_size |= static_cast<size_t>(frame_bytes[size_offset++]) << (byte_index * 8u);
        }
        if (subframe_size == 0 || subframe_size > index_offset - payload_offset) {
            return 0;
        }
        if (!probe_vp9_header(frame_bytes.subspan(payload_offset, subframe_size)).valid) {
            return 0;
        }
        payload_offset += subframe_size;
    }
    return payload_offset == index_offset ? frame_count : 0;
}

bool is_valid_vp9_frame(std::span<const uint8_t> frame_bytes) noexcept {
    return vp9_subframe_count(frame_bytes) != 0;
}

} // namespace cricodecs::video
