/**
 * @file hca_encoder.cpp
 * @brief HCA Encoder Implementation
 *
 * Encoder structure is informed by VGAudio's public HCA encoder and then
 * checked against CRI SDK encoder binaries where recovered. Official encoder
 * binaries also expose session/callback/worker-thread/stream-sharing behavior;
 * this file implements only the supported in-tree encode path.
 *
 * Youjose additions over the VGAudio baseline include v3 comp-header encoding,
 * official lib band/HFR layout, official lib intensity encode, optional M/S
 * stereo encode, generated tables formulas.
 *
 * TODO:
 * - Test v1 encode behavior against official 0x0102/0x0103 output
 *   samples; 0x0101 remains unsupported.
 * - Check loop-padding and loop-frame alignment behavior more
 *   completely.
 */

#include "hca_codec.hpp"
#include "hca_crypto.hpp"
#include "hca_format.hpp"
#include "hca_frame.hpp"
#include "hca_packing.hpp"
#include "hca_tables.hpp"
#include "hca_transform.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

#include "../utilities/io.hpp"
#include "../utilities/numeric.hpp"

namespace cricodecs::hca {

namespace {

using cricodecs::util::align_up;
using cricodecs::util::divide_round_up;
using io::write_be;

constexpr uint32_t BASE_HEADER_SIZE = 96;
constexpr uint32_t BASE_HEADER_ALIGNMENT = 32;
constexpr uint32_t LOOP_FRAME_ALIGNMENT = 2048;
constexpr uint32_t HFR_GROUP_TARGET_BANDS = 8;
constexpr float MAX_SCALED_SPECTRA = 0.999999f;
constexpr float INTENSITY_ENERGY_SCALE = 16777216.0f;
constexpr float ONE_OVER_SQRT2 = 0.70710677f;

[[nodiscard]] uint32_t round_positive_band(double value) noexcept {
    return static_cast<uint32_t>(value + 0.5);
}

[[nodiscard]] uint32_t calculate_bitrate(const HcaHeader& info, HcaQuality quality) {
    const uint64_t pcm_bitrate =
        static_cast<uint64_t>(info.fmt.sample_rate) * info.fmt.channel_count * 16;
    const uint64_t frame_limited_bitrate =
        static_cast<uint64_t>(HCA_MAX_FRAME_SIZE) * info.fmt.sample_rate * 8 /
        HCA_SAMPLES_PER_FRAME;
    const uint32_t max_bitrate = static_cast<uint32_t>(
        std::min(pcm_bitrate / 4, frame_limited_bitrate));
    const bool joint_coding = info.fmt.channel_count != 1 &&
        info.codec.track_count != info.fmt.channel_count &&
        !info.codec.is_ambisonics();
    const uint32_t min_bitrate = static_cast<uint32_t>(std::min<uint64_t>(
        pcm_bitrate / 6,
        static_cast<uint64_t>(info.fmt.channel_count) *
            (joint_coding ? 32'000u : 42'666u)));

    int ratio = 6;
    switch (quality) {
        case HcaQuality::Highest: ratio = 4; break;
        case HcaQuality::High:    ratio = 6; break;
        case HcaQuality::Middle:  ratio = 8; break;
        case HcaQuality::Low:     ratio = (info.fmt.channel_count == 1) ? 10 : 12; break;
        case HcaQuality::Lowest:  ratio = (info.fmt.channel_count == 1) ? 12 : 16; break;
        default: break;
    }

    const uint32_t target = static_cast<uint32_t>(
        pcm_bitrate / static_cast<uint32_t>(ratio));
    return std::clamp(target, std::min(min_bitrate, max_bitrate), max_bitrate);
}

[[nodiscard]] bool calculate_band_counts(
    HcaHeader& info, uint32_t bitrate, uint32_t cutoff_freq)
{
    const uint64_t frame_size = static_cast<uint64_t>(bitrate) * HCA_SAMPLES_PER_FRAME /
        info.fmt.sample_rate / 8;
    if (frame_size > HCA_MAX_FRAME_SIZE) {
        return false;
    }
    info.codec.frame_size = static_cast<uint16_t>(frame_size);

    const uint32_t channel_count = std::max<uint32_t>(info.fmt.channel_count, 1);
    const uint32_t track_count = std::max<uint32_t>(info.codec.track_count, 1);
    const uint32_t channels_per_track = channel_count / track_count;
    const double sample_rate = static_cast<double>(info.fmt.sample_rate);
    const double cutoff = static_cast<double>(cutoff_freq);

    const uint32_t total_band_count = std::min<uint32_t>(
        HCA_SAMPLES_PER_SUBFRAME,
        round_positive_band(cutoff * 256.0 / sample_rate)
    );

    uint32_t base_band_count = total_band_count;
    uint32_t stereo_band_count = 0;
    uint32_t hfr_band_count = 0;

    if (total_band_count != 0) {
        const double bitrate_ratio =
            ((static_cast<double>(bitrate) / channel_count) / sample_rate) * 128.0 / total_band_count;
        const bool independent_channels =
            channels_per_track <= 1 || info.codec.is_ambisonics();

        if (independent_channels) {
            if (bitrate_ratio < 8.0 / 3.0) {
                if (bitrate_ratio < 4.0 / 3.0) {
                    base_band_count = total_band_count / 2;
                } else {
                    const double limited_cutoff = std::min(cutoff, 6.0 * bitrate / (32.0 * channel_count));
                    base_band_count = round_positive_band(limited_cutoff * 256.0 / sample_rate);
                }
            }

            uint32_t hfr_start_band = base_band_count + 1;
            if (base_band_count >= total_band_count - base_band_count) {
                hfr_start_band = base_band_count;
            }
            hfr_start_band = std::min(hfr_start_band, total_band_count);
            base_band_count = hfr_start_band;
            hfr_band_count = total_band_count - hfr_start_band;
        } else if (bitrate_ratio < 8.0 / 3.0) {
            if (bitrate_ratio >= 2.0) {
                const double limited_cutoff = std::min(cutoff, 6.0 * bitrate / (32.0 * channel_count));
                const double stereo_start = std::max(0.0, (limited_cutoff - (cutoff - limited_cutoff)) * 256.0 / sample_rate);
                base_band_count = std::min(total_band_count, round_positive_band(stereo_start));
                stereo_band_count = total_band_count - base_band_count;
            } else {
                uint32_t hfr_start_band = bitrate_ratio < 1.0
                    ? total_band_count / 2
                    : round_positive_band(std::min(cutoff, 8.0 * bitrate / (32.0 * channel_count)) * 256.0 / sample_rate);

                uint32_t adjusted_hfr_start = hfr_start_band + 1;
                if (hfr_start_band >= total_band_count - hfr_start_band) {
                    adjusted_hfr_start = hfr_start_band;
                }
                adjusted_hfr_start = std::min(adjusted_hfr_start, total_band_count);
                base_band_count = adjusted_hfr_start / 2 + (adjusted_hfr_start & 1u);
                stereo_band_count = adjusted_hfr_start - base_band_count;
                hfr_band_count = total_band_count - adjusted_hfr_start;
            }
        }
    }

    const uint32_t bands_per_group = divide_round_up(hfr_band_count, HFR_GROUP_TARGET_BANDS);
    const uint32_t group_count = bands_per_group > 0
        ? divide_round_up(hfr_band_count, bands_per_group)
        : 0;

    info.codec.total_band_count = static_cast<uint8_t>(total_band_count);
    info.codec.base_band_count = static_cast<uint8_t>(base_band_count);
    info.codec.stereo_band_count = static_cast<uint8_t>(stereo_band_count);
    info.codec.hfr_group_count = static_cast<uint8_t>(group_count);
    info.codec.bands_per_hfr_group = static_cast<uint8_t>(bands_per_group);
    return true;
}

[[nodiscard]] constexpr uint8_t ambisonics_order_for_channels(uint32_t channels) noexcept {
    // Full-sphere Ambisonics carries (order + 1)^2 channels.
    for (uint8_t order = 1; order <= HCA_MAX_AMBISONICS_ORDER; ++order) {
        const uint32_t side = static_cast<uint32_t>(order) + 1;
        if (side * side == channels) {
            return order;
        }
    }
    return 0;
}

[[nodiscard]] std::optional<uint8_t> default_channel_configuration(
    uint8_t channels_per_track, bool ambisonics) noexcept
{
    if (ambisonics) {
        const uint8_t order = ambisonics_order_for_channels(channels_per_track);
        return order == 0
            ? std::nullopt
            : std::optional<uint8_t>{static_cast<uint8_t>(HcaCodecChunk::ambisonics_flag | order)};
    }
    if (channels_per_track < tables::DEFAULT_CHANNEL_MAPPING.size()) {
        return tables::DEFAULT_CHANNEL_MAPPING[channels_per_track];
    }
    return uint8_t{0};
}

[[nodiscard]] bool is_supported_channel_configuration(
    uint8_t channels_per_track, uint8_t channel_config) noexcept
{
    if ((channel_config & HcaCodecChunk::ambisonics_flag) != 0) {
        return true;
    }
    if (channels_per_track <= tables::VALID_CHANNEL_MAPPINGS.size()) {
        return channel_config < tables::VALID_CHANNEL_MAPPINGS[0].size() &&
            tables::VALID_CHANNEL_MAPPINGS[channels_per_track - 1][channel_config] != 0;
    }

    // The official encoder accepts config 0 through the fmt channel limit, and its
    // role builder also defines config 3 layouts for 10, 12, and 16 channels.
    return channel_config == 0 ||
        (channel_config == 3 &&
            (channels_per_track == 10 || channels_per_track == 12 || channels_per_track == 16));
}

[[nodiscard]] bool set_channel_configuration(
    HcaHeader& info, const HcaEncodeConfig& config) noexcept
{
    if (info.codec.track_count == 0 || info.fmt.channel_count % info.codec.track_count != 0) {
        return false;
    }

    const uint8_t channels_per_track = static_cast<uint8_t>(info.fmt.channel_count / info.codec.track_count);
    if (channels_per_track == 0) {
        return false;
    }

    const auto default_config = default_channel_configuration(channels_per_track, config.ambisonics);
    if (!config.channel_config && !default_config) {
        return false;
    }
    const uint8_t channel_config = config.channel_config
        ? *config.channel_config
        : *default_config;
    if (((channel_config & HcaCodecChunk::ambisonics_flag) != 0) != config.ambisonics) {
        return false;
    }
    if (!is_supported_channel_configuration(channels_per_track, channel_config)) {
        return false;
    }

    info.codec.channel_config = channel_config;
    return true;
}

void initialize_frame(EncoderFrame& frame) {
    for (uint32_t c = 0; c < frame.info.fmt.channel_count; ++c) {
        auto& channel = frame.channels[c];
        channel.type = detail::channel_type(frame.info, c);
        channel.coded_count = channel.type == ChannelType::StereoSecondary
            ? frame.info.codec.base_band_count
            : static_cast<uint8_t>(frame.info.codec.base_band_count + frame.info.codec.stereo_band_count);
    }
}

void calculate_loop_info(HcaHeader& info, uint32_t loop_start, uint32_t loop_end) {
    loop_start += info.fmt.encoder_delay;
    loop_end += info.fmt.encoder_delay;

    info.loop.start_frame = loop_start / HCA_SAMPLES_PER_FRAME;
    info.loop.start_delay = static_cast<uint16_t>(loop_start % HCA_SAMPLES_PER_FRAME);
    info.loop.end_frame = loop_end / HCA_SAMPLES_PER_FRAME;
    info.loop.end_padding = static_cast<uint16_t>(HCA_SAMPLES_PER_FRAME - (loop_end % HCA_SAMPLES_PER_FRAME));

    if (info.loop.end_padding == HCA_SAMPLES_PER_FRAME) {
        info.loop.end_frame--;
        info.loop.end_padding = 0;
    }
}

void calculate_header_size(HcaHeader& info) {
    // HCA headers in this format are serialized on a 32-byte boundary.
    info.file.header_size = static_cast<uint16_t>(align_up(BASE_HEADER_SIZE, BASE_HEADER_ALIGNMENT));
    if (!info.loop.enabled()) {
        return;
    }

    const uint32_t loop_frame_offset = info.file.header_size + info.codec.frame_size * info.loop.start_frame;
    // Loop start metadata is written on 2048-byte boundaries in encoder outputs,
    // so align forward before attaching loop frame offsets.
    // This keeps loop metadata placement consistent with observed writers.
    const uint32_t padded_offset = align_up(loop_frame_offset, LOOP_FRAME_ALIGNMENT);
    const uint32_t padding_bytes = padded_offset - loop_frame_offset;
    const uint32_t padding_frames = padding_bytes / info.codec.frame_size;

    info.fmt.encoder_delay = static_cast<uint16_t>(info.fmt.encoder_delay + padding_frames * HCA_SAMPLES_PER_FRAME);
    info.loop.start_frame += padding_frames;
    info.loop.end_frame += padding_frames;
    info.file.header_size += padding_bytes % info.codec.frame_size;
}

std::expected<std::vector<int16_t>, std::string> build_looping_pcm(
    std::span<const int16_t> pcm_data,
    uint32_t sample_count,
    HcaHeader& info,
    const HcaEncodeConfig& config)
{
    const uint32_t loop_end = config.loop_end == 0 ? sample_count : config.loop_end;
    if (config.loop_start >= loop_end || loop_end > sample_count) {
        return std::unexpected(std::string("HCA encode failed: invalid loop range"));
    }

    const uint32_t loop_length = loop_end - config.loop_start;
    // CRI loop metadata is frame-based; align loop-start to frame boundaries
    // to avoid invalid loop sample offsets in generated headers.
    info.fmt.encoder_delay = static_cast<uint16_t>(
        info.fmt.encoder_delay + align_up(config.loop_start, HCA_SAMPLES_PER_FRAME) - config.loop_start);
    calculate_loop_info(info, config.loop_start, loop_end);

    // Loop end is aligned to sub-frame boundaries so trailing carry can be
    // emitted as valid full encoder frames, matching observed HCA writer
    // patterns produced by CRI-compatible writers.
    const uint32_t aligned_main_samples = std::min(
        align_up(loop_end, HCA_SAMPLES_PER_SUBFRAME), sample_count);
    // TODO: verify this post-loop carry against more official looped HCA samples.
    const uint32_t encoded_sample_count = aligned_main_samples + HCA_SAMPLES_PER_SUBFRAME * 2;
    std::vector<int16_t> output(static_cast<size_t>(encoded_sample_count) * info.fmt.channel_count, 0);

    const size_t main_samples = static_cast<size_t>(loop_end) * info.fmt.channel_count;
    std::copy_n(pcm_data.begin(), main_samples, output.begin());

    for (uint32_t i = 0; i < encoded_sample_count - loop_end; ++i) {
        const uint32_t source_sample = config.loop_start + (i % loop_length);
        for (uint32_t channel = 0; channel < info.fmt.channel_count; ++channel) {
            output[static_cast<size_t>(loop_end + i) * info.fmt.channel_count + channel] =
                pcm_data[static_cast<size_t>(source_sample) * info.fmt.channel_count + channel];
        }
    }

    calculate_header_size(info);
    return output;
}

void mdct_transform(EncoderChannel& channel, int subframe) {
    constexpr int size = HCA_SAMPLES_PER_SUBFRAME;
    constexpr int half = size / 2;

    const auto& window = tables::IMDCT_WINDOW;
    float* wave = channel.wave[subframe].data();
    float* previous = channel.imdct_previous.data();
    std::array<float, size * 2> windowed{};
    std::array<float, size> first{};
    std::array<float, size> second{};

    for (int i = 0; i < size; ++i) {
        windowed[static_cast<size_t>(i)] = std::abs(window[static_cast<size_t>(i)]) * previous[i];
        windowed[static_cast<size_t>(size + i)] =
            std::abs(window[static_cast<size_t>(size - i - 1)]) * wave[i];
    }

    for (int i = 0; i < size; ++i) {
        first[static_cast<size_t>(i)] = -windowed[static_cast<size_t>(size + half - i - 1)];
        second[static_cast<size_t>(i)] = i < half
            ? -windowed[static_cast<size_t>(size + half + i)]
            : windowed[static_cast<size_t>(i - half)];
    }

    const auto first_dct = transform::dct4(first, transform::HCA_DCT4_MDCT_SCALE);
    const auto second_dct = transform::dct4(second, transform::HCA_DCT4_MDCT_SCALE);
    for (int i = 0; i < size; ++i) {
        channel.spectra[subframe][static_cast<size_t>(i)] =
            first_dct[static_cast<size_t>(i)] + second_dct[static_cast<size_t>(i)];
    }

    std::memcpy(previous, wave, size * sizeof(float));
}

[[nodiscard]] int find_scalefactor(float value) noexcept {
    const auto& scales = tables::DEQUANTIZER_SCALING_TABLE;
    int low = 0;
    int high = 63;
    while (low < high) {
        const int mid = (low + high) / 2;
        if (scales[mid] <= value) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low;
}

void calculate_scalefactors(EncoderFrame& frame) {
    for (uint32_t c = 0; c < frame.info.fmt.channel_count; ++c) {
        auto& channel = frame.channels[c];
        const uint8_t analysis_count = channel.type == ChannelType::StereoSecondary
            ? channel.coded_count
            : frame.info.codec.total_band_count;
        for (uint8_t band = 0; band < analysis_count; ++band) {
            float max_value = 0.0f;
            for (int subframe = 0; subframe < HCA_SUBFRAMES; ++subframe) {
                max_value = std::max(max_value, std::abs(channel.spectra[subframe][band]));
            }
            channel.scalefactors[band] = static_cast<uint8_t>(find_scalefactor(max_value));
        }
        std::fill(channel.scalefactors.begin() + analysis_count, channel.scalefactors.end(), uint8_t{0});
    }
}

[[nodiscard]] int calculate_resolution(
    int scalefactor, int noise_level, int min_resolution, int max_resolution) noexcept {
    if (scalefactor == 0) {
        return 0;
    }

    const int position = noise_level - 5 * scalefactor / 2 + 1;
    if (position < 0) {
        return max_resolution;
    }
    if (position >= static_cast<int>(tables::RESOLUTION_INVERT_TABLE.size())) {
        return min_resolution;
    }
    return std::min<int>(tables::RESOLUTION_INVERT_TABLE[static_cast<size_t>(position)], max_resolution);
}

void scale_spectra(EncoderFrame& frame) {
    const auto& info = frame.info;
    const int hfr_start_band = info.codec.base_band_count + info.codec.stereo_band_count;

    for (uint32_t c = 0; c < info.fmt.channel_count; ++c) {
        auto& channel = frame.channels[c];
        std::array<float, 8> hfr_averages{};
        if (channel.type != ChannelType::StereoSecondary) {
            for (int group = 0, band = hfr_start_band;
                 group < info.codec.hfr_group_count;
                 ++group) {
                float sum = 0.0f;
                int count = 0;
                for (int i = 0; i < info.codec.bands_per_hfr_group
                     && band < info.codec.total_band_count; ++i, ++band) {
                    for (int subframe = 0; subframe < HCA_SUBFRAMES; ++subframe) {
                        const float value = channel.spectra[subframe][band];
                        sum += value * value;
                    }
                    count += HCA_SUBFRAMES;
                }
                hfr_averages[group] = count > 0 ? std::sqrt(sum / count) : 0.0f;
            }
        }

        const uint8_t analysis_count = channel.type == ChannelType::StereoSecondary
            ? channel.coded_count
            : info.codec.total_band_count;
        for (uint8_t band = 0; band < analysis_count; ++band) {
            const uint8_t scalefactor = channel.scalefactors[band];
            const float scale = tables::QUANTIZER_SCALING_TABLE[scalefactor];
            for (int subframe = 0; subframe < HCA_SUBFRAMES; ++subframe) {
                auto& value = channel.spectra[subframe][band];
                value = scalefactor == 0
                    ? 0.0f
                    : cricodecs::util::clamp(
                          value * scale, -MAX_SCALED_SPECTRA, MAX_SCALED_SPECTRA);
            }
        }

        if (channel.type == ChannelType::StereoSecondary) {
            continue;
        }
        int low_band = hfr_start_band - 1;
        int high_band = hfr_start_band;
        const int group_half = info.codec.hfr_group_count >> 1;
        for (int group = 0;
             group < info.codec.hfr_group_count;
            ++group) {
            float low_sum = 0.0f;
            float high_sum = 0.0f;
            const bool ascending = detail::uses_v3_frame_layout(info.file.version) && group >= group_half;
            for (int i = 0; i < info.codec.bands_per_hfr_group && high_band < info.codec.total_band_count;
                 ++i, ++high_band) {
                if (low_band < 0 || low_band >= hfr_start_band) {
                    break;
                }
                for (int subframe = 0; subframe < HCA_SUBFRAMES; ++subframe) {
                    const float low = channel.spectra[subframe][low_band];
                    const float high = channel.spectra[subframe][high_band];
                    low_sum += low * low;
                    high_sum += high * high;
                }
                low_band += ascending ? 1 : -1;
            }

            float group_average = hfr_averages[group];
            if (low_sum > 0.0f && high_sum > 0.0f) {
                group_average *= std::min(std::sqrt(high_sum / low_sum), std::numbers::sqrt2_v<float>);
            }
            channel.hfr_scales[group] = static_cast<uint8_t>(find_scalefactor(group_average));
        }
    }
}

void prepare_versioned_hfr_scalefactors(EncoderFrame& frame) {
    if (!detail::uses_v3_frame_layout(frame.info.file.version) || frame.info.codec.hfr_group_count == 0) {
        return;
    }

    for (uint32_t c = 0; c < frame.info.fmt.channel_count; ++c) {
        auto& channel = frame.channels[c];
        if (channel.type == ChannelType::StereoSecondary) {
            continue;
        }

        for (uint8_t group = 0; group < frame.info.codec.hfr_group_count; ++group) {
            channel.scalefactors[static_cast<size_t>(channel.coded_count) + group] = channel.hfr_scales[group];
        }
    }
}

void encode_intensity_stereo(EncoderFrame& frame) {
    if (frame.info.codec.stereo_band_count == 0) {
        return;
    }

    const int first_joint_band = frame.info.codec.base_band_count;
    const int band_limit = frame.info.codec.total_band_count;

    for (uint32_t c = 0; c + 1 < frame.info.fmt.channel_count; ++c) {
        auto& primary = frame.channels[c];
        auto& secondary = frame.channels[c + 1];
        if (primary.type != ChannelType::StereoPrimary || secondary.type != ChannelType::StereoSecondary) {
            continue;
        }

        for (int subframe = 0; subframe < HCA_SUBFRAMES; ++subframe) {
            int left_sum = 0;
            int right_sum = 0;
            int combined_abs_sum = 0;

            for (int band = first_joint_band; band < band_limit; ++band) {
                const int left = static_cast<int>(primary.spectra[subframe][band] * INTENSITY_ENERGY_SCALE);
                const int right = static_cast<int>(secondary.spectra[subframe][band] * INTENSITY_ENERGY_SCALE);
                left_sum += std::abs(left);
                right_sum += std::abs(right);
                combined_abs_sum += std::abs(left + right);
            }

            float energy_scale = 1.0f;
            int quantized = 0;
            const int energy_sum = left_sum + right_sum;
            if (energy_sum > 0) {
                const float stored_value = static_cast<float>(2.0 * left_sum / energy_sum);
                if (combined_abs_sum > 0) {
                    energy_scale = cricodecs::util::clamp(
                        static_cast<float>(static_cast<double>(energy_sum) / combined_abs_sum),
                        1.0f,
                        std::numbers::sqrt2_v<float>);
                }

                while (quantized < static_cast<int>(tables::INTENSITY_RATIO_BOUNDS.size())
                    && stored_value <= tables::INTENSITY_RATIO_BOUNDS[quantized]) {
                    ++quantized;
                }
                if (quantized == 0 && right_sum > 0) {
                    quantized = 1;
                } else if (quantized >= static_cast<int>(tables::INTENSITY_RATIO_BOUNDS.size()) && left_sum > 0) {
                    quantized = static_cast<int>(tables::INTENSITY_RATIO_BOUNDS.size()) - 1;
                }
            }

            secondary.intensity[subframe] = static_cast<uint8_t>(quantized);
            for (int band = first_joint_band; band < band_limit; ++band) {
                const float left = primary.spectra[subframe][band];
                const float right = secondary.spectra[subframe][band];
                const float average = (left + right) * 0.5f;
                const float side = (left - right) * 0.5f * ONE_OVER_SQRT2;
                primary.spectra[subframe][band] =
                    (side * side <= average * average ? average : side) * energy_scale;
                secondary.spectra[subframe][band] = 0.0f;
            }
        }
    }
}

void encode_ms_stereo(EncoderFrame& frame) {
    if (!frame.info.codec.uses_ms_stereo() || frame.info.codec.stereo_band_count == 0) {
        return;
    }

    const int band_limit = frame.info.codec.base_band_count;
    for (uint32_t c = 0; c + 1 < frame.info.fmt.channel_count; ++c) {
        auto& primary = frame.channels[c];
        auto& secondary = frame.channels[c + 1];
        if (primary.type != ChannelType::StereoPrimary || secondary.type != ChannelType::StereoSecondary) {
            continue;
        }

        for (int subframe = 0; subframe < HCA_SUBFRAMES; ++subframe) {
            for (int band = 0; band < band_limit; ++band) {
                const float left = primary.spectra[subframe][band];
                const float right = secondary.spectra[subframe][band];
                primary.spectra[subframe][band] = (left + right) * ONE_OVER_SQRT2;
                secondary.spectra[subframe][band] = (left - right) * ONE_OVER_SQRT2;
            }
        }
    }
}

[[nodiscard]] int legacy_or_v2_intensity_bits() noexcept {
    return HCA_SUBFRAMES * 4;
}

[[nodiscard]] int v3_intensity_bits(const EncoderChannel& channel) noexcept {
    return std::all_of(
        channel.intensity.begin(),
        channel.intensity.end(),
        [](uint8_t value) { return value == 7; })
        ? 4
        : 4 + 2 + (HCA_SUBFRAMES - 1) * 4;
}

void calculate_frame_header_length(EncoderFrame& frame) {
    for (uint32_t c = 0; c < frame.info.fmt.channel_count; ++c) {
        auto& channel = frame.channels[c];
        const auto count = packing::scalefactor_count_for_header(frame.info, channel);
        const auto encoding = packing::scalefactor_encoding(std::span(channel.scalefactors).first(count));
        channel.header_length_bits = static_cast<int>(encoding.bit_count);
        channel.scalefactor_delta_bits = encoding.delta_bits;
        if (channel.type == ChannelType::StereoSecondary) {
            channel.header_length_bits += detail::uses_v3_frame_layout(frame.info.file.version)
                ? v3_intensity_bits(channel)
                : legacy_or_v2_intensity_bits();
        } else if (!detail::uses_v3_frame_layout(frame.info.file.version) && frame.info.codec.hfr_group_count > 0) {
            channel.header_length_bits += 6 * frame.info.codec.hfr_group_count;
        }
    }
}

[[nodiscard]] int band_bit_count(
    const EncoderChannel& channel, uint8_t band, uint8_t resolution) noexcept {
    if (resolution == 0) return 0;
    int bits = 0;
    if (resolution >= 8) {
        const int base = tables::QUANTIZED_SPECTRUM_MAX_BITS[resolution] - 1;
        const float dead_zone = tables::QUANTIZER_DEAD_ZONE[resolution];
        for (int subframe = 0; subframe < HCA_SUBFRAMES; ++subframe) {
            bits += base + (std::abs(channel.spectra[subframe][band]) >= dead_zone);
        }
        return bits;
    }

    const float step_inv = tables::QUANTIZER_INVERSE_STEP_SIZE[resolution];
    const float shift_up = step_inv + 1.0f;
    const int shift_down = static_cast<int>(step_inv + 0.5f - 8.0f);
    for (int subframe = 0; subframe < HCA_SUBFRAMES; ++subframe) {
        const int index = static_cast<int>(channel.spectra[subframe][band] * step_inv + shift_up)
            - shift_down - 8;
        const auto* code = tables::quantized_spectrum_code(resolution, index);
        if (code == nullptr) return std::numeric_limits<int>::max();
        bits += code->bit_count;
    }
    return bits;
}

[[nodiscard]] int calculate_used_bits(const EncoderFrame& frame, int noise_level, int evaluation_boundary) {
    int total_bits = 16 + 16;

    for (uint32_t c = 0; c < frame.info.fmt.channel_count; ++c) {
        const auto& channel = frame.channels[c];
        total_bits += channel.header_length_bits;

        for (uint8_t band = 0; band < channel.coded_count; ++band) {
            const int noise = band < evaluation_boundary ? noise_level - 1 : noise_level;
            const uint8_t resolution = static_cast<uint8_t>(calculate_resolution(
                channel.scalefactors[band], noise,
                frame.info.codec.min_resolution, frame.info.codec.max_resolution));
            const int bits = band_bit_count(channel, band, resolution);
            if (bits == std::numeric_limits<int>::max()) return bits;
            total_bits += bits;
        }
    }

    return ((total_bits + 7) & ~7) + 16;
}

[[nodiscard]] std::array<int, HCA_SAMPLES_PER_SUBFRAME> boundary_bit_counts(
    const EncoderFrame& frame, int noise_level) {
    std::array<int64_t, HCA_SAMPLES_PER_SUBFRAME> deltas{};
    std::array<int, HCA_SAMPLES_PER_SUBFRAME> invalid_deltas{};
    int64_t base = 16 + 16;
    int invalid = 0;
    for (uint32_t c = 0; c < frame.info.fmt.channel_count; ++c) {
        const auto& channel = frame.channels[c];
        base += channel.header_length_bits;
        for (uint8_t band = 0; band < channel.coded_count; ++band) {
            const auto normal_resolution = static_cast<uint8_t>(
                calculate_resolution(channel.scalefactors[band], noise_level,
                    frame.info.codec.min_resolution, frame.info.codec.max_resolution));
            const auto boosted_resolution = static_cast<uint8_t>(
                calculate_resolution(channel.scalefactors[band], noise_level - 1,
                    frame.info.codec.min_resolution, frame.info.codec.max_resolution));
            const int normal = band_bit_count(channel, band, normal_resolution);
            const int boosted = band_bit_count(channel, band, boosted_resolution);
            const bool normal_invalid = normal == std::numeric_limits<int>::max();
            const bool boosted_invalid = boosted == std::numeric_limits<int>::max();
            invalid += normal_invalid;
            invalid_deltas[band] += static_cast<int>(boosted_invalid) - static_cast<int>(normal_invalid);
            if (!normal_invalid) base += normal;
            deltas[band] += (boosted_invalid ? 0 : boosted) - (normal_invalid ? 0 : normal);
        }
    }

    std::array<int, HCA_SAMPLES_PER_SUBFRAME> result{};
    for (size_t boundary = 0; boundary < result.size(); ++boundary) {
        result[boundary] = invalid == 0
            ? static_cast<int>(std::min<int64_t>(((base + 7) & ~int64_t{7}) + 16,
                                                 std::numeric_limits<int>::max()))
            : std::numeric_limits<int>::max();
        base += deltas[boundary];
        invalid += invalid_deltas[boundary];
    }
    return result;
}

[[nodiscard]] int binary_search_level(const EncoderFrame& frame, int available_bits, int low, int high) {
    const int max = high;
    int mid_value = 0;

    while (low != high) {
        const int mid = (low + high) / 2;
        mid_value = calculate_used_bits(frame, mid, 0);
        if (mid_value > available_bits) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    return low == max && mid_value > available_bits ? -1 : low;
}

[[nodiscard]] int binary_search_boundary(
    const std::array<int, HCA_SAMPLES_PER_SUBFRAME>& bit_counts,
    int available_bits,
    int low,
    int high) {
    int low_value = bit_counts[low];
    int high_value = bit_counts[high];

    do {
        const int mid = (low + high) / 2;
        const int mid_value = bit_counts[mid];
        if (mid_value > available_bits) {
            high = mid;
            high_value = mid_value;
        } else {
            low = mid;
            low_value = mid_value;
        }
    } while (high - low > 1);

    if (low_value > available_bits) {
        return -1;
    }
    return high_value > available_bits ? low : high;
}

[[nodiscard]] bool calculate_noise_level(EncoderFrame& frame) {
    // The SDK centers this search around the previous noise level; this encoder
    // is stateless between frames, so it searches the full local range.
    int highest_band = frame.info.codec.base_band_count + frame.info.codec.stereo_band_count - 1;
    const int available_bits = frame.info.codec.frame_size * 8;
    int level = binary_search_level(frame, available_bits, 0, 255);

    while (level < 0) {
        highest_band -= 2;
        if (highest_band < 0) {
            return false;
        }

        for (uint32_t c = 0; c < frame.info.fmt.channel_count; ++c) {
            auto& channel = frame.channels[c];
            if (highest_band + 1 < HCA_SAMPLES_PER_SUBFRAME) {
                channel.scalefactors[highest_band + 1] = 0;
            }
            if (highest_band + 2 < HCA_SAMPLES_PER_SUBFRAME) {
                channel.scalefactors[highest_band + 2] = 0;
            }
        }

        calculate_frame_header_length(frame);
        level = binary_search_level(frame, available_bits, 0, 255);
    }

    frame.acceptable_noise_level = level;
    return true;
}

[[nodiscard]] bool calculate_evaluation_boundary(EncoderFrame& frame) {
    if (frame.acceptable_noise_level == 0) {
        frame.evaluation_boundary = 0;
        return true;
    }

    const int available_bits = frame.info.codec.frame_size * 8;
    const auto bit_counts = boundary_bit_counts(frame, frame.acceptable_noise_level);
    const int level = binary_search_boundary(bit_counts, available_bits, 0, 127);
    if (level < 0) {
        return false;
    }

    frame.evaluation_boundary = level;
    return true;
}

void calculate_frame_resolutions(EncoderFrame& frame) {
    for (uint32_t c = 0; c < frame.info.fmt.channel_count; ++c) {
        auto& channel = frame.channels[c];
        for (uint8_t band = 0; band < channel.coded_count; ++band) {
            const int noise = band < frame.evaluation_boundary
                ? frame.acceptable_noise_level - 1
                : frame.acceptable_noise_level;
            channel.resolution[band] = static_cast<uint8_t>(calculate_resolution(
                channel.scalefactors[band], noise,
                frame.info.codec.min_resolution, frame.info.codec.max_resolution));
        }
        std::fill(channel.resolution.begin() + channel.coded_count, channel.resolution.end(), uint8_t{0});
    }
}

void quantize_spectra(EncoderFrame& frame) {
    for (uint32_t c = 0; c < frame.info.fmt.channel_count; ++c) {
        auto& channel = frame.channels[c];
        for (uint8_t band = 0; band < channel.coded_count; ++band) {
            const uint8_t resolution = channel.resolution[band];
            const float step_inv = tables::QUANTIZER_INVERSE_STEP_SIZE[resolution];
            const float shift_up = step_inv + 1.0f;
            const int shift_down = static_cast<int>(step_inv + 0.5f);
            for (int subframe = 0; subframe < HCA_SUBFRAMES; ++subframe) {
                channel.quantized_spectra[subframe][band] =
                    static_cast<int>(channel.spectra[subframe][band] * step_inv + shift_up) - shift_down;
            }
        }
    }
}

size_t write_format_chunk(const HcaHeader& info, uint8_t* buffer) {
    write_be(buffer + sizeof(detail::HcaFileRecord), detail::HcaFormatRecord{
        HCA_CHUNK_ID_FMT,
        static_cast<uint32_t>(info.fmt.channel_count) << 24 | info.fmt.sample_rate,
        info.fmt.frame_count,
        info.fmt.encoder_delay,
        info.fmt.encoder_padding,
    });
    return sizeof(detail::HcaFileRecord) + sizeof(detail::HcaFormatRecord);
}

size_t write_comp_chunk(const HcaHeader& info, uint8_t* buffer, size_t position) {
    write_be(buffer + position, detail::HcaCompRecord{
        HCA_CHUNK_ID_COMP,
        info.codec.frame_size,
        info.codec.min_resolution,
        info.codec.max_resolution,
        info.codec.track_count,
        info.codec.channel_config,
        info.codec.total_band_count,
        info.codec.base_band_count,
        info.codec.stereo_band_count,
        info.codec.bands_per_hfr_group,
        info.codec.ms_stereo(),
        0,
    });
    return position + sizeof(detail::HcaCompRecord);
}

size_t write_dec_chunk(const HcaHeader& info, uint8_t* buffer, size_t position) {
    write_be(buffer + position, detail::HcaDecRecord{
        HCA_CHUNK_ID_DEC,
        info.codec.frame_size,
        info.codec.min_resolution,
        info.codec.max_resolution,
        static_cast<uint8_t>(info.codec.total_band_count - 1),
        static_cast<uint8_t>((info.codec.stereo_band_count > 0
            ? info.codec.base_band_count : info.codec.total_band_count) - 1),
        static_cast<uint8_t>((info.codec.track_count << 4) | (info.codec.channel_config & 0x0F)),
        static_cast<uint8_t>(info.codec.stereo_band_count > 0),
    });
    return position + sizeof(detail::HcaDecRecord);
}

size_t write_ath_chunk(const HcaHeader& info, uint8_t* buffer, size_t position) {
    write_be<uint32_t>(buffer + position, HCA_CHUNK_ID_ATH);
    write_be(buffer + position + 4, HcaAthChunk{
        detail::explicit_ath_type(info.file.version, info.ath.uses_curve())});
    return position + 6;
}

void pack_header(const HcaHeader& info, uint8_t* buffer) {
    std::memset(buffer, 0, info.file.header_size);

    write_be(buffer, detail::HcaFileRecord{
        HCA_CHUNK_ID_HCA, info.file.version, info.file.header_size});

    size_t position = write_format_chunk(info, buffer);
    if (detail::uses_dec_header(info.file.version)) {
        position = write_dec_chunk(info, buffer, position);
    } else {
        position = write_comp_chunk(info, buffer, position);
    }

    if (detail::writes_explicit_ath_chunk(info.file.version)) {
        position = write_ath_chunk(info, buffer, position);
    }

    if (info.loop.enabled()) {
        write_be(buffer + position, detail::HcaLoopRecord{
            HCA_CHUNK_ID_LOOP,
            info.loop.start_frame,
            info.loop.end_frame,
            info.loop.start_delay,
            info.loop.end_padding,
        });
        position += sizeof(detail::HcaLoopRecord);
    }

    write_be<uint32_t>(buffer + position, HCA_CHUNK_ID_CIPH);
    write_be(buffer + position + 4, info.cipher);
    position += 6;

    write_be<uint32_t>(buffer + position, HCA_CHUNK_ID_PAD);

    const uint16_t crc = tables::crc16_checksum(buffer, info.file.header_size - 2);
    write_be<uint16_t>(buffer + info.file.header_size - 2, crc);
}

} // namespace

std::expected<std::vector<uint8_t>, std::string> encode(
    std::span<const int16_t> pcm_data,
    const HcaEncodeConfig& config)
{
    if (config.channel_count == 0 || config.channel_count > HCA_MAX_CHANNELS || config.sample_rate == 0) {
        return std::unexpected(std::string("HCA encode failed: channel count and sample rate must be valid"));
    }
    if (!detail::supports_encoder_version(config.version)) {
        return std::unexpected(std::string("HCA encode failed: unsupported encoder version"));
    }
    if (pcm_data.size() % config.channel_count != 0) {
        return std::unexpected(std::string("HCA encode failed: PCM sample count is not divisible by channel count"));
    }

    const uint32_t source_sample_count = static_cast<uint32_t>(pcm_data.size() / config.channel_count);

    HcaHeader info;
    info.file.version = config.version;
    info.fmt.sample_rate = config.sample_rate;
    info.fmt.channel_count = config.channel_count;
    info.codec.set_type(detail::uses_dec_header(info.file.version) ? HcaCodecChunkType::Dec : HcaCodecChunkType::Comp);
    info.codec.track_count = 1;
    info.codec.min_resolution = detail::uses_v3_frame_layout(info.file.version) ? 0 : 1;
    info.codec.max_resolution = 15;
    info.fmt.encoder_delay = HCA_SAMPLES_PER_SUBFRAME;
    info.ath.type = detail::explicit_ath_type(info.file.version, false);
    if (!set_channel_configuration(info, config)) {
        return std::unexpected(std::string("HCA encode failed: unsupported channel configuration"));
    }
    if (detail::uses_dec_header(info.file.version) && info.codec.is_ambisonics()) {
        return std::unexpected(std::string("HCA encode failed: Ambisonics requires comp headers"));
    }

    const uint32_t bitrate = config.bitrate > 0 ? config.bitrate : calculate_bitrate(info, config.quality);
    if (!calculate_band_counts(info, bitrate, config.sample_rate / 2) ||
        info.codec.frame_size < HCA_MIN_FRAME_SIZE ||
        info.codec.total_band_count > HCA_SAMPLES_PER_SUBFRAME) {
        return std::unexpected(std::string("HCA encode failed: calculated frame layout is invalid"));
    }
    if (detail::uses_dec_header(info.file.version) && info.codec.hfr_group_count != 0) {
        // Legacy dec headers don't carry HFR grouping metadata, so low-bitrate
        // requests that need HFR stay explicitly unsupported for now.
        return std::unexpected(std::string("HCA encode failed: HFR is not implemented for legacy dec headers"));
    }
    if (config.ms_stereo) {
        if (detail::uses_dec_header(info.file.version)) {
            return std::unexpected(std::string("HCA encode failed: M/S stereo requires comp headers"));
        }
        if (info.codec.stereo_band_count == 0) {
            return std::unexpected(std::string("HCA encode failed: M/S stereo requires a stereo band layout"));
        }
    }
    info.codec.set_ms_stereo(config.ms_stereo);

    std::span<const int16_t> source_pcm = pcm_data;
    std::vector<int16_t> loop_pcm;
    uint32_t encoded_sample_count = source_sample_count;

    if (config.loop_enabled) {
        auto prepared_loop_pcm = build_looping_pcm(pcm_data, source_sample_count, info, config);
        if (!prepared_loop_pcm) {
            return std::unexpected(prepared_loop_pcm.error());
        }
        loop_pcm = std::move(*prepared_loop_pcm);
        source_pcm = std::span<const int16_t>(loop_pcm);
        encoded_sample_count = static_cast<uint32_t>(source_pcm.size() / info.fmt.channel_count);
    } else {
        // Non-looped output also keeps the legacy 32-byte header alignment,
        // following the same HCA header layout assumptions.
        info.file.header_size = static_cast<uint16_t>(align_up(BASE_HEADER_SIZE, BASE_HEADER_ALIGNMENT));
    }

    const uint32_t total_samples = encoded_sample_count + info.fmt.encoder_delay;
    info.fmt.frame_count = static_cast<uint32_t>(
        divide_round_up(total_samples, static_cast<uint32_t>(HCA_SAMPLES_PER_FRAME)));
    info.fmt.encoder_padding = static_cast<uint16_t>(info.fmt.frame_count * HCA_SAMPLES_PER_FRAME - total_samples);

    std::vector<uint8_t> output(static_cast<size_t>(info.file.header_size) + static_cast<size_t>(info.fmt.frame_count) * info.codec.frame_size, 0);
    pack_header(info, output.data());

    EncoderFrame frame(info);
    initialize_frame(frame);

    uint8_t* frame_ptr = output.data() + info.file.header_size;
    for (uint32_t frame_index = 0; frame_index < info.fmt.frame_count; ++frame_index) {
        for (int subframe = 0; subframe < HCA_SUBFRAMES; ++subframe) {
            for (int sample = 0; sample < HCA_SAMPLES_PER_SUBFRAME; ++sample) {
                const size_t encoded_index = static_cast<size_t>(frame_index) * HCA_SAMPLES_PER_FRAME +
                    subframe * HCA_SAMPLES_PER_SUBFRAME + sample;

                for (uint32_t channel = 0; channel < info.fmt.channel_count; ++channel) {
                    float pcm_sample = 0.0f;
                    const size_t source_index = encoded_index;
                    if (source_index < encoded_sample_count) {
                        pcm_sample = source_pcm[source_index * info.fmt.channel_count + channel] / 32768.0f;
                    }
                    frame.channels[channel].wave[subframe][sample] = pcm_sample;
                }
            }
        }

        for (uint32_t channel = 0; channel < info.fmt.channel_count; ++channel) {
            for (int subframe = 0; subframe < HCA_SUBFRAMES; ++subframe) {
                mdct_transform(frame.channels[channel], subframe);
            }
        }

        encode_intensity_stereo(frame);
        encode_ms_stereo(frame);
        calculate_scalefactors(frame);
        scale_spectra(frame);
        prepare_versioned_hfr_scalefactors(frame);
        calculate_frame_header_length(frame);

        if (!calculate_noise_level(frame) || !calculate_evaluation_boundary(frame)) {
            return std::unexpected(std::string("HCA encode failed: could not calculate frame noise boundaries"));
        }

        calculate_frame_resolutions(frame);
        quantize_spectra(frame);
        packing::pack_frame(frame, frame_ptr);
        frame_ptr += info.codec.frame_size;
    }

    if (config.keycode != 0) {
        auto crypt_result = detail::encrypt_in_place(output, info, 56, config.keycode, config.subkey);
        if (!crypt_result) {
            return std::unexpected(crypt_result.error());
        }
    }

    return output;
}

std::expected<std::vector<uint8_t>, std::string> encode(
    const wav::WavContainer& wav,
    const HcaEncodeConfig& config
) {
    auto pcm = wav.get_pcm16();
    if (!pcm) {
        return std::unexpected("HCA encode failed: " + pcm.error());
    }

    auto effective_config = config;
    effective_config.sample_rate = wav.sample_rate();
    effective_config.channel_count = static_cast<uint8_t>(wav.channels());
    return encode(*pcm, effective_config);
}

} // namespace cricodecs::hca
