#pragma once
#include "hca_format.hpp"
#include "hca_frame.hpp"
#include "hca_tables.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

#include "../utilities/io_reader.hpp"

namespace cricodecs::hca::packing {

struct ScalefactorEncoding {
    uint8_t delta_bits = 0;
    size_t bit_count = 3;
};

struct ScalefactorDecoding {
    uint32_t wraps = 0;
    uint8_t delta_bits = 0;
};

[[nodiscard]] inline ScalefactorDecoding read_scalefactors(
    io::bit_reader& reader, std::span<uint8_t> values) noexcept {
    ScalefactorDecoding result;
    result.delta_bits = static_cast<uint8_t>(reader.read(3));
    if (result.delta_bits == 0) {
        std::ranges::fill(values, uint8_t{0});
        return result;
    }
    if (values.empty()) {
        return result;
    }
    if (result.delta_bits >= 6) {
        for (auto& value : values) {
            value = static_cast<uint8_t>(reader.read(6));
        }
        return result;
    }

    values[0] = static_cast<uint8_t>(reader.read(6));
    const uint8_t escape = static_cast<uint8_t>((1u << result.delta_bits) - 1u);
    const int bias = escape >> 1;
    for (size_t index = 1; index < values.size(); ++index) {
        const uint8_t delta = static_cast<uint8_t>(reader.read(result.delta_bits));
        if (delta == escape) {
            values[index] = static_cast<uint8_t>(reader.read(6));
            continue;
        }
        const int value = static_cast<int>(values[index - 1]) + delta - bias;
        result.wraps += value < 0 || value > 63;
        values[index] = static_cast<uint8_t>(value & 0x3F);
    }
    return result;
}

[[nodiscard]] inline bool read_intensity(
    io::bit_reader& reader,
    uint16_t version,
    std::span<uint8_t, HCA_SUBFRAMES> intensity) noexcept {
    const uint8_t first = static_cast<uint8_t>(reader.peek(4));
    if (version <= HCA_VERSION_V200) {
        intensity[0] = first;
        if (first < 15) {
            for (auto& value : intensity) {
                value = static_cast<uint8_t>(reader.read(4));
            }
        }
        return reader.valid();
    }

    intensity[0] = static_cast<uint8_t>(reader.read(4));
    if (first >= 15) {
        std::ranges::fill(intensity, uint8_t{7});
        return reader.valid();
    }

    const uint8_t delta_bits = static_cast<uint8_t>(reader.read(2));
    if (delta_bits == 3) {
        for (auto& value : intensity.subspan(1)) {
            value = static_cast<uint8_t>(reader.read(4));
        }
        return reader.valid();
    }

    const uint8_t escape = static_cast<uint8_t>((2u << delta_bits) - 1u);
    int current = first;
    for (auto& value : intensity.subspan(1)) {
        const uint8_t delta = static_cast<uint8_t>(reader.read(delta_bits + 1));
        current = delta == escape
            ? static_cast<int>(reader.read(4))
            : current + delta - (escape >> 1);
        if (current < 0 || current > 15) {
            return false;
        }
        value = static_cast<uint8_t>(current);
    }
    return reader.valid();
}

[[nodiscard]] inline ScalefactorEncoding scalefactor_encoding(
    std::span<const uint8_t> scales) noexcept {
    if (scales.empty() || std::ranges::all_of(scales, [](uint8_t value) { return value == 0; })) {
        return {};
    }

    ScalefactorEncoding best{.delta_bits = 6, .bit_count = 3 + 6 * scales.size()};
    for (uint8_t bits = 1; bits < 6; ++bits) {
        const int maximum_delta = tables::SCALEFACTOR_DELTA_LIMITS[bits - 1];
        size_t length = 9;
        for (size_t index = 1; index < scales.size(); ++index) {
            const int delta = static_cast<int>(scales[index]) - scales[index - 1];
            length += bits + (std::abs(delta) > maximum_delta ? 6 : 0);
        }
        if (length < best.bit_count) {
            best = {.delta_bits = bits, .bit_count = length};
        }
    }
    return best;
}

[[nodiscard]] inline uint8_t scalefactor_count_for_header(
    const HcaHeader& info, const EncoderChannel& channel) noexcept {
    if (detail::uses_v3_frame_layout(info.file.version) && channel.type != ChannelType::StereoSecondary) {
        return static_cast<uint8_t>(channel.coded_count + info.codec.hfr_group_count);
    }
    return channel.coded_count;
}

void pack_frame(EncoderFrame& frame, uint8_t* buffer);

} // namespace cricodecs::hca::packing
