/**
 * @file mpeg.cpp
 * @brief MPEG elementary-stream parser for USM frame packaging.
 */

#include "mpeg.hpp"

#include <cstring>
#include <limits>

namespace cricodecs::video {

namespace {

constexpr size_t npos = std::numeric_limits<size_t>::max();

[[nodiscard]] size_t find_start_code3(std::span<const uint8_t> bytes, size_t offset = 0) noexcept {
    const auto* data = bytes.data();
    const size_t size = bytes.size();
    while (offset + 3u <= size) {
        const auto* zero = static_cast<const uint8_t*>(std::memchr(data + offset, 0, size - offset - 2u));
        if (zero == nullptr) {
            return npos;
        }

        offset = static_cast<size_t>(zero - data);
        if (data[offset + 1u] == 0 && data[offset + 2u] == 1) {
            return offset;
        }
        ++offset;
    }
    return npos;
}

[[nodiscard]] bool is_start_code_at(std::span<const uint8_t> bytes, size_t offset) noexcept {
    return offset + 4u <= bytes.size() &&
        bytes[offset + 0u] == 0x00 &&
        bytes[offset + 1u] == 0x00 &&
        bytes[offset + 2u] == 0x01;
}

[[nodiscard]] bool is_picture_prefix_header(uint8_t start_code) noexcept {
    return start_code == 0xB2 || start_code == 0xB3 || start_code == 0xB5 || start_code == 0xB8;
}

[[nodiscard]] bool is_intra_picture_at(std::span<const uint8_t> bytes, size_t picture_offset) noexcept {
    if (picture_offset + 6u > bytes.size() || !is_start_code_at(bytes, picture_offset) || bytes[picture_offset + 3u] != 0x00) {
        return false;
    }

    const uint8_t picture_coding_type = static_cast<uint8_t>((bytes[picture_offset + 5u] >> 3u) & 0x07u);
    return picture_coding_type == 1u;
}

[[nodiscard]] MpegVideoSequenceHeader read_sequence_header_at(std::span<const uint8_t> bytes, size_t offset) noexcept {
    return MpegVideoSequenceHeader{
        .width = static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset + 4u]) << 4u) | (bytes[offset + 5u] >> 4u)),
        .height = static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset + 5u] & 0x0Fu) << 8u) | bytes[offset + 6u]),
        .aspect_ratio_code = static_cast<uint8_t>((bytes[offset + 7u] >> 4u) & 0x0Fu),
        .frame_rate_code = static_cast<uint8_t>(bytes[offset + 7u] & 0x0Fu),
        .bit_rate_value = static_cast<uint32_t>((static_cast<uint32_t>(bytes[offset + 8u]) << 10u) |
            (static_cast<uint32_t>(bytes[offset + 9u]) << 2u) |
            (static_cast<uint32_t>(bytes[offset + 10u]) >> 6u)),
    };
}

struct MpegScanResult {
    std::expected<MpegVideoSequenceHeader, std::string> sequence_header;
    MpegVideoType video_type = MpegVideoType::unknown;
    std::vector<MpegFrameRange> frames;
};

[[nodiscard]] MpegScanResult scan_mpeg_stream(std::span<const uint8_t> bytes) {
    MpegScanResult result{
        .sequence_header = std::unexpected("MPEG video parse failed: sequence header not found"),
        .video_type = MpegVideoType::unknown,
        .frames = {},
    };
    bool saw_sequence = false;
    size_t current_frame_start = npos;
    size_t next_frame_start = npos;
    bool current_keyframe = false;

    for (size_t offset = find_start_code3(bytes); offset != npos; offset = find_start_code3(bytes, offset + 3u)) {
        const uint8_t start_code = bytes[offset + 3u];
        if (start_code == 0xB3) {
            saw_sequence = true;
            if (offset + 12u <= bytes.size() && !result.sequence_header) {
                result.sequence_header = read_sequence_header_at(bytes, offset);
            }
        } else if (start_code == 0xB5 && saw_sequence && offset + 5u <= bytes.size()) {
            const uint8_t extension_id = static_cast<uint8_t>((bytes[offset + 4u] >> 4u) & 0x0Fu);
            if (extension_id == 0x01) {
                result.video_type = MpegVideoType::mpeg2;
            }
        }

        if (start_code == 0x00) {
            if (current_frame_start != npos) {
                const size_t frame_end = next_frame_start != npos ? next_frame_start : offset;
                if (frame_end > current_frame_start) {
                    result.frames.push_back(MpegFrameRange{
                        .offset = current_frame_start,
                        .size = frame_end - current_frame_start,
                        .is_keyframe = current_keyframe,
                    });
                }
            }

            current_frame_start = next_frame_start != npos ? next_frame_start : offset;
            next_frame_start = npos;
            current_keyframe = is_intra_picture_at(bytes, offset);
        } else if (is_picture_prefix_header(start_code) && next_frame_start == npos) {
            next_frame_start = offset;
        }
    }

    if (result.video_type == MpegVideoType::unknown && saw_sequence) {
        result.video_type = MpegVideoType::mpeg1;
    }

    if (current_frame_start != npos && current_frame_start < bytes.size()) {
        result.frames.push_back(MpegFrameRange{
            .offset = current_frame_start,
            .size = bytes.size() - current_frame_start,
            .is_keyframe = current_keyframe,
        });
    }

    if (result.frames.empty() && !bytes.empty()) {
        result.frames.push_back(MpegFrameRange{
            .offset = 0,
            .size = bytes.size(),
            .is_keyframe = true,
        });
    }

    return result;
}

} // namespace

MpegStructure inspect_mpeg_structure(std::span<const uint8_t> bytes) noexcept {
    MpegStructure structure;
    for (size_t offset = find_start_code3(bytes); offset != npos; offset = find_start_code3(bytes, offset + 3u)) {
        ++structure.start_codes;
        const uint8_t code = bytes[offset + 3u];
        const bool is_slice = code >= 0x01u && code <= 0xAFu;
        const bool is_known = code == 0x00u || is_slice || code == 0xB2u ||
            code == 0xB3u || code == 0xB5u || code == 0xB7u || code == 0xB8u;
        structure.valid_start_codes += is_known;
        structure.slices += is_slice;
        structure.violations += !is_known;

        if (code == 0x00u) {
            if (offset + 6u > bytes.size()) {
                ++structure.violations;
                continue;
            }
            const uint8_t coding_type = static_cast<uint8_t>((bytes[offset + 5u] >> 3u) & 0x07u);
            if (coding_type >= 1u && coding_type <= 4u) {
                ++structure.pictures;
            } else {
                ++structure.violations;
            }
        } else if (code == 0xB3u) {
            if (offset + 12u > bytes.size()) {
                ++structure.violations;
                continue;
            }
            const auto header = read_sequence_header_at(bytes, offset);
            if (header.width == 0 || header.height == 0 || header.aspect_ratio_code == 0 ||
                header.frame_rate_code == 0 || header.frame_rate_code > 8u ||
                (bytes[offset + 10u] & 0x20u) == 0) {
                ++structure.violations;
            }
        } else if (code == 0xB5u) {
            if (offset + 5u > bytes.size()) {
                ++structure.violations;
                continue;
            }
            const uint8_t extension_id = static_cast<uint8_t>(bytes[offset + 4u] >> 4u);
            const bool valid_extension = extension_id == 1u || extension_id == 2u ||
                extension_id == 3u || extension_id == 4u || extension_id == 5u ||
                extension_id == 7u || extension_id == 8u || extension_id == 9u || extension_id == 10u;
            structure.violations += !valid_extension;
        }
    }
    return structure;
}

std::expected<MpegVideoSequenceHeader, std::string> parse_mpeg_sequence_header(std::span<const uint8_t> bytes) {
    for (size_t offset = find_start_code3(bytes); offset != npos; offset = find_start_code3(bytes, offset + 3u)) {
        if (offset + 12u > bytes.size() || bytes[offset + 3u] != 0xB3) {
            continue;
        }

        return read_sequence_header_at(bytes, offset);
    }

    return std::unexpected("MPEG video parse failed: sequence header not found");
}

MpegVideoType detect_mpeg_video_type(std::span<const uint8_t> bytes) noexcept {
    bool saw_sequence = false;
    for (size_t offset = find_start_code3(bytes); offset != npos; offset = find_start_code3(bytes, offset + 3u)) {
        const uint8_t start_code = bytes[offset + 3u];
        if (start_code == 0xB3) {
            saw_sequence = true;
        } else if (start_code == 0xB5 && saw_sequence && offset + 5u <= bytes.size()) {
            const uint8_t extension_id = static_cast<uint8_t>((bytes[offset + 4u] >> 4u) & 0x0Fu);
            if (extension_id == 0x01) {
                return MpegVideoType::mpeg2;
            }
        }
    }

    return saw_sequence ? MpegVideoType::mpeg1 : MpegVideoType::unknown;
}

std::expected<void, std::string> MpegVideoReader::open(const std::filesystem::path& path) {
    if (auto result = m_reader.open(path); !result) {
        return std::unexpected("MPEG video load failed: could not open input file: " + path.string());
    }
    return parse_loaded_stream(path.string());
}

std::expected<void, std::string> MpegVideoReader::open(std::span<const uint8_t> bytes) {
    if (auto result = m_reader.open(bytes); !result) {
        return std::unexpected("MPEG video load failed: could not open memory buffer");
    }
    return parse_loaded_stream("memory buffer");
}

std::expected<void, std::string> MpegVideoReader::parse_loaded_stream(std::string_view source_name) {
    auto scan = scan_mpeg_stream(m_reader.data());
    if (!scan.sequence_header) {
        return std::unexpected("MPEG video load failed for " + std::string(source_name) + ": " + scan.sequence_header.error());
    }

    m_sequence_header = *scan.sequence_header;
    m_video_type = scan.video_type;
    m_frames = std::move(scan.frames);
    m_current_frame = 0;
    return {};
}

std::expected<MpegVideoFrame, std::string> MpegVideoReader::read_next_frame() {
    if (!has_frames()) {
        return std::unexpected("EOF");
    }

    const auto& range = m_frames[m_current_frame];
    const auto bytes = m_reader.subspan(range.offset, range.size);
    MpegVideoFrame frame{
        .size = static_cast<uint32_t>(bytes.size()),
        .index = m_current_frame,
        .is_keyframe = range.is_keyframe,
        .data = bytes,
        .record_bytes = bytes,
    };
    ++m_current_frame;
    return frame;
}

} // namespace cricodecs::video
