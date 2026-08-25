/**
 * @file h264.cpp
 * @brief H.264 Annex B structural parser for USM frame packaging.
 */

#include "h264.hpp"
#include "../utilities/byte_scan.hpp"

#include <algorithm>
#include <numeric>
#include <ranges>

namespace cricodecs::video {

namespace {

constexpr size_t npos = simd::npos;

struct NalUnit {
    size_t offset = 0;
    size_t payload_offset = 0;
    size_t end = 0;
    uint8_t type = 0;
};

struct AnnexBStartCode {
    size_t offset = npos;
    size_t size = 0;
};

[[nodiscard]] AnnexBStartCode find_annex_b_start_code(std::span<const uint8_t> bytes, size_t offset = 0) noexcept {
    const size_t marker = simd::find_zero_zero_one(bytes, offset);
    if (marker == npos) {
        return {};
    }
    if (marker > offset && bytes[marker - 1] == 0) {
        return AnnexBStartCode{.offset = marker - 1, .size = 4};
    }
    return AnnexBStartCode{.offset = marker, .size = 3};
}

class RbspReader {
public:
    explicit RbspReader(std::span<const uint8_t> data) : m_reader(data) {}

    [[nodiscard]] uint32_t bits(int count) noexcept { return m_reader.read(count); }
    [[nodiscard]] bool bit() noexcept { return bits(1) != 0; }

    [[nodiscard]] uint32_t ue() noexcept {
        uint32_t zeros = 0;
        bool stop = false;
        while (m_reader.remaining() != 0) {
            if (bit()) {
                stop = true;
                break;
            }
            if (++zeros > 31u) {
                m_valid = false;
                return 0;
            }
        }
        if (!stop) {
            m_valid = false;
            return 0;
        }
        return zeros == 0 ? 0u : ((1u << zeros) - 1u) + bits(static_cast<int>(zeros));
    }

    [[nodiscard]] int32_t se() noexcept {
        const uint32_t code = ue();
        const int32_t value = static_cast<int32_t>((code + 1u) / 2u);
        return (code & 1u) != 0 ? value : -value;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return m_valid && m_reader.valid();
    }

private:
    io::bit_reader m_reader;
    bool m_valid = true;
};

[[nodiscard]] bool is_vcl_nal(uint8_t nal_type) noexcept {
    return nal_type >= 1u && nal_type <= 5u;
}

[[nodiscard]] std::vector<NalUnit> find_annex_b_nals(std::span<const uint8_t> bytes) {
    std::vector<NalUnit> nals;
    for (auto start_code = find_annex_b_start_code(bytes);
        start_code.offset != npos;
        start_code = find_annex_b_start_code(bytes, start_code.offset + start_code.size)) {

        const size_t payload_offset = start_code.offset + start_code.size;
        if (payload_offset >= bytes.size()) {
            break;
        }
        nals.push_back(NalUnit{
            .offset = start_code.offset,
            .payload_offset = payload_offset,
            .end = bytes.size(),
            .type = static_cast<uint8_t>(bytes[payload_offset] & 0x1Fu),
        });
    }

    for (size_t index = 0; index + 1u < nals.size(); ++index) {
        nals[index].end = nals[index + 1u].offset;
    }
    return nals;
}

[[nodiscard]] std::vector<uint8_t> make_rbsp(std::span<const uint8_t> nal_payload) {
    std::vector<uint8_t> rbsp;
    rbsp.reserve(nal_payload.size());
    uint8_t zero_count = 0;
    for (const uint8_t byte : nal_payload) {
        if (zero_count >= 2u && byte == 0x03u) {
            zero_count = 0;
            continue;
        }
        rbsp.push_back(byte);
        zero_count = byte == 0 ? static_cast<uint8_t>(zero_count + 1u) : 0u;
    }
    return rbsp;
}

void skip_scaling_list(RbspReader& br, uint32_t size) {
    int32_t last_scale = 8;
    int32_t next_scale = 8;
    for (uint32_t index = 0; index < size; ++index) {
        if (next_scale != 0) {
            next_scale = (last_scale + br.se() + 256) % 256;
        }
        last_scale = next_scale == 0 ? last_scale : next_scale;
    }
}

void skip_vui_prefix_before_timing(RbspReader& br) {
    if (br.bit()) {
        if (br.bits(8) == 255u) {
            static_cast<void>(br.bits(16));
            static_cast<void>(br.bits(16));
        }
    }

    if (br.bit()) {
        static_cast<void>(br.bit());
    }

    if (br.bit()) {
        static_cast<void>(br.bits(3));
        static_cast<void>(br.bit());
        if (br.bit()) {
            static_cast<void>(br.bits(8));
            static_cast<void>(br.bits(8));
            static_cast<void>(br.bits(8));
        }
    }

    if (br.bit()) {
        static_cast<void>(br.ue());
        static_cast<void>(br.ue());
    }
}

} // namespace

H264Structure inspect_h264_structure(std::span<const uint8_t> bytes) noexcept {
    H264Structure structure;
    const auto nals = find_annex_b_nals(bytes);
    structure.nal_units = static_cast<uint32_t>(nals.size());

    for (const auto& nal : nals) {
        const uint8_t header = bytes[nal.payload_offset];
        const uint8_t nal_ref_idc = static_cast<uint8_t>((header >> 5u) & 0x03u);
        const uint8_t nal_type = static_cast<uint8_t>(header & 0x1Fu);
        const bool reference_forbidden = nal_ref_idc != 0u &&
            (nal_type == 6u || nal_type == 9u || nal_type == 10u ||
             nal_type == 11u || nal_type == 12u);
        const bool reserved_type = (nal_type >= 16u && nal_type <= 18u) ||
            nal_type == 22u || nal_type == 23u;
        const bool valid_header = (header & 0x80u) == 0 && nal_type >= 1u &&
            nal_type <= 23u && !reserved_type && !reference_forbidden;
        structure.valid_nal_headers += valid_header;

        size_t payload_end = nal.end;
        while (payload_end > nal.payload_offset + 1u && bytes[payload_end - 1u] == 0) {
            --payload_end;
        }

        const auto ebsp = bytes.subspan(nal.payload_offset + 1u, payload_end - nal.payload_offset - 1u);
        for (size_t offset = 2; offset < ebsp.size(); ++offset) {
            if (ebsp[offset - 2u] != 0 || ebsp[offset - 1u] != 0) {
                continue;
            }
            if (ebsp[offset] == 0x03u && offset + 1u < ebsp.size() && ebsp[offset + 1u] <= 0x03u) {
                ++structure.emulation_prevention_bytes;
            } else if (ebsp[offset] <= 0x02u || ebsp[offset] == 0x03u) {
                ++structure.ebsp_violations;
            }
        }

        if (!valid_header || !is_vcl_nal(nal_type) || ebsp.empty()) {
            continue;
        }
        const auto rbsp = make_rbsp(ebsp);
        RbspReader br(rbsp);
        static_cast<void>(br.ue());
        const auto slice_type = br.ue();
        const auto pic_parameter_set_id = br.ue();
        if (br && slice_type <= 9u && pic_parameter_set_id <= 255u) {
            ++structure.valid_slice_headers;
        }
    }
    return structure;
}

std::expected<H264SequenceParameterSet, std::string> parse_h264_sequence_parameter_set(std::span<const uint8_t> bytes) {
    const auto nals = find_annex_b_nals(bytes);
    const auto sps_nal = std::ranges::find_if(nals, [](const NalUnit& nal) {
        return nal.type == 7u;
    });
    if (sps_nal == nals.end() || sps_nal->payload_offset + 1u >= sps_nal->end) {
        return std::unexpected("H.264 video parse failed: SPS NAL not found");
    }

    const auto rbsp = make_rbsp(bytes.subspan(sps_nal->payload_offset + 1u, sps_nal->end - sps_nal->payload_offset - 1u));
    RbspReader br(rbsp);

    const auto profile_idc = br.bits(8);
    static_cast<void>(br.bits(8));
    const auto level_idc = br.bits(8);
    static_cast<void>(br.ue());
    if (!br) {
        return std::unexpected("H.264 video parse failed: truncated SPS header");
    }

    uint32_t chroma_format_idc = 1;
    bool separate_colour_plane_flag = false;
    switch (profile_idc) {
        case 100:
        case 110:
        case 122:
        case 244:
        case 44:
        case 83:
        case 86:
        case 118:
        case 128:
        case 138:
        case 139:
        case 134: {
            chroma_format_idc = br.ue();
            if (chroma_format_idc == 3u) {
                separate_colour_plane_flag = br.bit();
            }
            static_cast<void>(br.ue());
            static_cast<void>(br.ue());
            static_cast<void>(br.bit());
            if (br.bit()) {
                const uint32_t scaling_list_count = chroma_format_idc != 3u ? 8u : 12u;
                for (uint32_t index = 0; index < scaling_list_count; ++index) {
                    if (br.bit()) {
                        skip_scaling_list(br, index < 6u ? 16u : 64u);
                    }
                }
            }
            if (!br) {
                return std::unexpected("H.264 video parse failed: truncated SPS profile data");
            }
            break;
        }
        default:
            break;
    }

    static_cast<void>(br.ue());
    const auto pic_order_cnt_type = br.ue();
    if (pic_order_cnt_type == 0u) {
        static_cast<void>(br.ue());
    } else if (pic_order_cnt_type == 1u) {
        static_cast<void>(br.bit());
        static_cast<void>(br.se());
        static_cast<void>(br.se());
        const auto cycle_size = br.ue();
        for (uint32_t index = 0; index < cycle_size && br; ++index) {
            static_cast<void>(br.se());
        }
    }

    static_cast<void>(br.ue());
    static_cast<void>(br.bit());
    const auto pic_width_in_mbs_minus1 = br.ue();
    const auto pic_height_in_map_units_minus1 = br.ue();
    const auto frame_mbs_only_flag = br.bit();
    if (!br) {
        return std::unexpected("H.264 video parse failed: truncated SPS dimensions");
    }
    if (!frame_mbs_only_flag) {
        static_cast<void>(br.bit());
    }
    static_cast<void>(br.bit());

    uint32_t frame_crop_left_offset = 0;
    uint32_t frame_crop_right_offset = 0;
    uint32_t frame_crop_top_offset = 0;
    uint32_t frame_crop_bottom_offset = 0;
    if (br.bit()) {
        frame_crop_left_offset = br.ue();
        frame_crop_right_offset = br.ue();
        frame_crop_top_offset = br.ue();
        frame_crop_bottom_offset = br.ue();
        if (!br) {
            return std::unexpected("H.264 video parse failed: truncated SPS crop rectangle");
        }
    }

    uint32_t num_units_in_tick = 0;
    uint32_t time_scale = 0;
    bool fixed_frame_rate = false;
    if (br.bit()) {
        skip_vui_prefix_before_timing(br);
        if (br.bit()) {
            num_units_in_tick = br.bits(32);
            time_scale = br.bits(32);
            fixed_frame_rate = br.bit();
            if (!br) {
                return std::unexpected("H.264 video parse failed: truncated SPS timing info");
            }
        }
    }
    if (!br) {
        return std::unexpected("H.264 video parse failed: truncated SPS data");
    }

    uint32_t width = (pic_width_in_mbs_minus1 + 1u) * 16u;
    uint32_t height = (2u - static_cast<uint32_t>(frame_mbs_only_flag)) * (pic_height_in_map_units_minus1 + 1u) * 16u;
    const uint32_t chroma_array_type = separate_colour_plane_flag ? 0u : chroma_format_idc;
    uint32_t crop_unit_x = 1;
    uint32_t crop_unit_y = 2u - static_cast<uint32_t>(frame_mbs_only_flag);
    if (chroma_array_type != 0u) {
        const uint32_t sub_width_c = chroma_array_type == 3u ? 1u : 2u;
        const uint32_t sub_height_c = chroma_array_type == 1u ? 2u : 1u;
        crop_unit_x = sub_width_c;
        crop_unit_y = sub_height_c * (2u - static_cast<uint32_t>(frame_mbs_only_flag));
    }
    width -= std::min(width, (frame_crop_left_offset + frame_crop_right_offset) * crop_unit_x);
    height -= std::min(height, (frame_crop_top_offset + frame_crop_bottom_offset) * crop_unit_y);

    return H264SequenceParameterSet{
        .width = static_cast<uint16_t>(width),
        .height = static_cast<uint16_t>(height),
        .profile_idc = static_cast<uint8_t>(profile_idc),
        .level_idc = static_cast<uint8_t>(level_idc),
        .num_units_in_tick = num_units_in_tick,
        .time_scale = time_scale,
        .fixed_frame_rate = fixed_frame_rate,
    };
}

std::expected<void, std::string> H264VideoReader::open(const std::filesystem::path& path) {
    if (auto result = m_reader.open(path); !result) {
        return std::unexpected("H.264 video load failed: could not open input file: " + path.string());
    }
    return parse_loaded_stream(path.string());
}

std::expected<void, std::string> H264VideoReader::open(std::span<const uint8_t> bytes) {
    if (auto result = m_reader.open(bytes); !result) {
        return std::unexpected("H.264 video load failed: could not open memory buffer");
    }
    return parse_loaded_stream("memory buffer");
}

std::pair<uint32_t, uint32_t> H264VideoReader::frame_rate() const noexcept {
    if (m_sps.num_units_in_tick != 0 && m_sps.time_scale != 0) {
        const uint32_t denominator = m_sps.num_units_in_tick * 2u;
        const uint32_t divisor = std::gcd(m_sps.time_scale, denominator);
        return {m_sps.time_scale / divisor, denominator / divisor};
    }
    return {30000, 1001};
}

std::expected<void, std::string> H264VideoReader::parse_loaded_stream(std::string_view source_name) {
    auto sps = parse_h264_sequence_parameter_set(m_reader.data());
    if (!sps) {
        return std::unexpected("H.264 video load failed for " + std::string(source_name) + ": " + sps.error());
    }

    m_sps = *sps;
    m_frames = split_frames(m_reader.data());
    m_current_frame = 0;
    if (m_frames.empty()) {
        return std::unexpected("H.264 video load failed for " + std::string(source_name) + ": no frames found");
    }
    return {};
}

std::vector<H264VideoReader::FrameRange> H264VideoReader::split_frames(std::span<const uint8_t> bytes) {
    const auto nals = find_annex_b_nals(bytes);
    std::vector<FrameRange> frames;
    frames.reserve(nals.size());
    if (nals.empty()) {
        return frames;
    }

    const bool has_aud = std::ranges::any_of(nals, [](const NalUnit& nal) {
        return nal.type == 9u;
    });

    size_t current_frame_start = npos;
    bool current_has_vcl = false;
    bool current_keyframe = false;

    auto finish_frame = [&](size_t frame_end) {
        if (current_frame_start != npos && frame_end > current_frame_start && current_has_vcl) {
            frames.push_back(FrameRange{
                .offset = current_frame_start,
                .size = frame_end - current_frame_start,
                .is_keyframe = current_keyframe,
            });
        }
    };

    if (has_aud) {
        for (const auto& nal : nals) {
            if (nal.type == 9u) {
                finish_frame(nal.offset);
                current_frame_start = nal.offset;
                current_has_vcl = false;
                current_keyframe = false;
                continue;
            }
            if (current_frame_start == npos) {
                current_frame_start = nal.offset;
            }
            if (is_vcl_nal(nal.type)) {
                current_has_vcl = true;
                current_keyframe = current_keyframe || nal.type == 5u;
            }
        }
        finish_frame(bytes.size());
        return frames;
    }

    size_t pending_prefix_start = npos;
    for (const auto& nal : nals) {
        if (current_frame_start == npos) {
            current_frame_start = nal.offset;
        }

        if (is_vcl_nal(nal.type)) {
            if (current_has_vcl) {
                const size_t boundary = pending_prefix_start != npos ? pending_prefix_start : nal.offset;
                finish_frame(boundary);
                current_frame_start = boundary;
                current_has_vcl = false;
                current_keyframe = false;
                pending_prefix_start = npos;
            }
            current_has_vcl = true;
            current_keyframe = current_keyframe || nal.type == 5u;
        } else if (current_has_vcl && pending_prefix_start == npos) {
            pending_prefix_start = nal.offset;
        }
    }
    finish_frame(bytes.size());
    return frames;
}

std::expected<H264VideoFrame, std::string> H264VideoReader::read_next_frame() {
    if (!has_frames()) {
        return std::unexpected("EOF");
    }

    const auto& range = m_frames[m_current_frame];
    const auto bytes = m_reader.subspan(range.offset, range.size);
    H264VideoFrame frame{
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
