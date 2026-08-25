#pragma once
/**
 * @file hca_frame.hpp
 * @brief HCA encoder frame state.
 */

#include "hca_header.hpp"

#include <array>
#include <cstdint>

namespace cricodecs::hca {

struct EncoderChannel {
    ChannelType type = ChannelType::Discrete;
    uint8_t coded_count = 0;

    std::array<uint8_t, HCA_SAMPLES_PER_SUBFRAME> scalefactors{};
    std::array<uint8_t, HCA_SAMPLES_PER_SUBFRAME> resolution{};
    std::array<uint8_t, 8> hfr_scales{};
    std::array<uint8_t, HCA_SUBFRAMES> intensity{};

    std::array<std::array<float, HCA_SAMPLES_PER_SUBFRAME>, HCA_SUBFRAMES> spectra{};
    std::array<std::array<int, HCA_SAMPLES_PER_SUBFRAME>, HCA_SUBFRAMES> quantized_spectra{};

    std::array<float, HCA_SAMPLES_PER_SUBFRAME> imdct_previous{};
    std::array<std::array<float, HCA_SAMPLES_PER_SUBFRAME>, HCA_SUBFRAMES> wave{};

    int header_length_bits = 0;
    int scalefactor_delta_bits = 0;
};

struct EncoderFrame {
    const HcaHeader& info;
    std::array<EncoderChannel, 8> channels;
    int acceptable_noise_level = 0;
    int evaluation_boundary = 0;
};

namespace detail {

[[nodiscard]] inline std::array<ChannelType, 8> channel_types(const HcaHeader& info) noexcept {
    std::array<ChannelType, 8> result{};
    const uint32_t channels_per_track = info.codec.track_count == 0
        ? 0u
        : info.fmt.channel_count / info.codec.track_count;
    if (info.codec.stereo_band_count == 0 || channels_per_track <= 1) {
        return result;
    }

    for (uint32_t track = 0; track < info.codec.track_count; ++track) {
        auto* types = result.data() + track * channels_per_track;
        switch (channels_per_track) {
            case 2:
            case 3:
                types[0] = ChannelType::StereoPrimary;
                types[1] = ChannelType::StereoSecondary;
                break;
            case 4:
                types[0] = ChannelType::StereoPrimary;
                types[1] = ChannelType::StereoSecondary;
                if (info.codec.channel_config == 0) {
                    types[2] = ChannelType::StereoPrimary;
                    types[3] = ChannelType::StereoSecondary;
                }
                break;
            case 5:
                types[0] = ChannelType::StereoPrimary;
                types[1] = ChannelType::StereoSecondary;
                if (info.codec.channel_config <= 2) {
                    types[3] = ChannelType::StereoPrimary;
                    types[4] = ChannelType::StereoSecondary;
                }
                break;
            case 6:
            case 7:
            case 8:
                types[0] = ChannelType::StereoPrimary;
                types[1] = ChannelType::StereoSecondary;
                types[4] = ChannelType::StereoPrimary;
                types[5] = ChannelType::StereoSecondary;
                if (channels_per_track == 8) {
                    types[6] = ChannelType::StereoPrimary;
                    types[7] = ChannelType::StereoSecondary;
                }
                break;
            default:
                break;
        }
    }
    return result;
}

} // namespace detail

} // namespace cricodecs::hca
