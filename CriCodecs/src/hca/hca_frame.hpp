#pragma once
/**
 * @file hca_frame.hpp
 * @brief HCA encoder frame state.
 */

#include "hca_header.hpp"

#include <array>
#include <cstdint>
#include <vector>

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
    std::vector<EncoderChannel> channels;
    int acceptable_noise_level = 0;
    int evaluation_boundary = 0;

    explicit EncoderFrame(const HcaHeader& header)
        : info(header), channels(header.fmt.channel_count) {}
};

namespace detail {

[[nodiscard]] inline ChannelType channel_type(
    const HcaHeader& info, uint32_t channel) noexcept
{
    const uint32_t channels_per_track = info.codec.track_count == 0
        ? 0u
        : info.fmt.channel_count / info.codec.track_count;
    if (info.codec.stereo_band_count == 0 || channels_per_track <= 1 ||
        channel >= channels_per_track * info.codec.track_count) {
        return ChannelType::Discrete;
    }

    const uint32_t local = channel % channels_per_track;
    if (info.codec.channel_config == 3 &&
        (channels_per_track == 10 || channels_per_track == 12 || channels_per_track == 16)) {
        if (channels_per_track == 10 && (local == 2 || local == 3)) {
            return ChannelType::Discrete;
        }
        return local % 2 == 0 ? ChannelType::StereoPrimary : ChannelType::StereoSecondary;
    }
    if (channels_per_track > 8) {
        return ChannelType::Discrete;
    }

    if (local == 0) {
        return ChannelType::StereoPrimary;
    }
    if (local == 1) {
        return ChannelType::StereoSecondary;
    }

    if (channels_per_track == 4 && info.codec.channel_config == 0) {
        return local == 2 ? ChannelType::StereoPrimary : ChannelType::StereoSecondary;
    }
    if (channels_per_track == 5 && info.codec.channel_config <= 2 && local >= 3) {
        return local == 3 ? ChannelType::StereoPrimary : ChannelType::StereoSecondary;
    }
    if (channels_per_track >= 6 && channels_per_track <= 8) {
        if (local == 4 || (channels_per_track == 8 && local == 6)) {
            return ChannelType::StereoPrimary;
        }
        if (local == 5 || (channels_per_track == 8 && local == 7)) {
            return ChannelType::StereoSecondary;
        }
    }
    return ChannelType::Discrete;
}

} // namespace detail

} // namespace cricodecs::hca
