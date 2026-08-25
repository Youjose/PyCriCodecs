#pragma once
#include "hca_format.hpp"
#include "hca_frame.hpp"
#include "hca_tables.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace cricodecs::hca::packing {

struct ScalefactorEncoding {
    uint8_t delta_bits = 0;
    size_t bit_count = 3;
};

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

[[nodiscard]] inline uint8_t scalefactor_count_for_header(const HcaHeader& info, const HcaChannel& channel) noexcept {
    if (detail::uses_v3_frame_layout(info.file.version) && channel.type != ChannelType::StereoSecondary) {
        return static_cast<uint8_t>(channel.coded_count + info.codec.hfr_group_count);
    }
    return channel.coded_count;
}

void pack_frame(HcaFrame& frame, uint8_t* buffer);

} // namespace cricodecs::hca::packing
