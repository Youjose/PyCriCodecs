/**
 * @file hca_decoder.cpp
 * @brief HCA Decoder Implementation
 *
 * Credits follow vgmstream's clHCA decoder provenance:
 * - Original decompilation and C++ decoder by nyaga
 *     https://github.com/Nyagamon/HCADecoder
 * - Ported to C by kode54
 *     https://gist.github.com/kode54/ce2bf799b445002e125f06ed833903c0
 * - Cleaned up and re-reverse engineered for HCA v3 by bnnm, using
 *   Thealexbarney's VGAudio decoder as reference
 *     https://github.com/Thealexbarney/VGAudio
 * - CriCodecs C++23 port by Youjose, including generated tables formulas.
 *
 */

#include "hca_crypto.hpp"
#include "hca_frame.hpp"
#include "hca_packing.hpp"
#include "hca_reader.hpp"
#include "hca_tables.hpp"
#include "hca_transform.hpp"

#include <array>

#include "../utilities/io_endian.hpp"
#include "../utilities/io_reader.hpp"
#include "../utilities/numeric.hpp"

namespace cricodecs::hca {

namespace {

using BitReader = io::bit_reader;
using io::read_be;

struct DecodeChannel {
    ChannelType type = ChannelType::Discrete;
    uint8_t coded_count = 0;
    std::array<uint8_t, HCA_SAMPLES_PER_SUBFRAME> scalefactors{};
    std::array<uint8_t, HCA_SAMPLES_PER_SUBFRAME> resolution{};
    std::array<float, HCA_SAMPLES_PER_SUBFRAME> gain{};
    std::array<uint8_t, 8> hfr_scales{};
    std::array<uint8_t, HCA_SUBFRAMES> intensity{};
    std::array<std::array<float, HCA_SAMPLES_PER_SUBFRAME>, HCA_SUBFRAMES> spectra{};
    std::array<float, HCA_SAMPLES_PER_SUBFRAME> imdct_previous{};
    std::array<std::array<float, HCA_SAMPLES_PER_SUBFRAME>, HCA_SUBFRAMES> wave{};
    std::array<uint8_t, HCA_SAMPLES_PER_SUBFRAME> noises{};
    uint8_t noise_count = 0;
    uint8_t valid_count = 0;
};

struct DecodeFrame {
    const HcaHeader& info;
    std::array<DecodeChannel, 8> channels;
    std::array<uint8_t, HCA_SAMPLES_PER_SUBFRAME> ath_curve{};
    uint32_t random = 1;
};

[[nodiscard]] float dequantizer_scale(uint8_t scalefactor) noexcept {
    return tables::DEQUANTIZER_SCALING_TABLE[scalefactor];
}

[[nodiscard]] float quantizer_step_size(uint8_t resolution) noexcept {
    if (resolution == 0) {
        return 1.0f;
    }
    const int max_value = resolution < 8 ? resolution : ((1 << (resolution - 4)) - 1);
    return 1.0f / (static_cast<float>(max_value) + 0.5f);
}

[[nodiscard]] float scale_conversion(int index) noexcept {
    if (index < 0 || index >= static_cast<int>(tables::SCALE_CONVERSION_TABLE.size())) {
        return 0.0f;
    }
    return tables::SCALE_CONVERSION_TABLE[static_cast<size_t>(index)];
}

void assign_channel_types(DecodeFrame& frame) {
    const auto& info = frame.info;
    const auto types = detail::channel_types(info);
    for (uint32_t i = 0; i < info.fmt.channel_count; ++i) {
        frame.channels[i].type = types[i];
        frame.channels[i].coded_count = frame.channels[i].type == ChannelType::StereoSecondary
            ? info.codec.base_band_count
            : static_cast<uint8_t>(info.codec.base_band_count + info.codec.stereo_band_count);
    }
}

bool unpack_scalefactors(DecodeChannel& ch, BitReader& br, int hfr_group_count, uint16_t version) {
    uint32_t coded_count = ch.coded_count;
    uint32_t extra_count = 0;
    if (ch.type != ChannelType::StereoSecondary && hfr_group_count > 0 && version > HCA_VERSION_V200) {
        extra_count = static_cast<uint32_t>(hfr_group_count);
        coded_count += extra_count;
        if (coded_count > HCA_SAMPLES_PER_SUBFRAME) {
            return false;
        }
    }

    const auto values = std::span(ch.scalefactors).first(coded_count);
    static_cast<void>(packing::read_scalefactors(br, values));
    if (!br.valid()) {
        return false;
    }
    std::fill(ch.scalefactors.begin() + coded_count, ch.scalefactors.end(), uint8_t{0});

    for (uint32_t i = 0; i < extra_count; ++i) {
        ch.hfr_scales[i] = ch.scalefactors[ch.coded_count + i];
    }
    return true;
}

bool unpack_intensity(DecodeChannel& ch, BitReader& br, int hfr_group_count, uint16_t version) {
    if (ch.type == ChannelType::StereoSecondary) {
        return packing::read_intensity(br, version, ch.intensity);
    }

    if (version <= HCA_VERSION_V200) {
        for (int i = 0; i < hfr_group_count; ++i) {
            ch.hfr_scales[i] = static_cast<uint8_t>(br.read(6));
        }
    }
    return br.valid();
}

void calculate_resolution(DecodeChannel& ch, int packed_noise_level, const uint8_t* ath_curve,
                          int min_res, int max_res) {
    unsigned int noise_count = 0;
    unsigned int valid_count = 0;

    for (uint32_t i = 0; i < ch.coded_count; ++i) {
        uint8_t new_resolution = 0;
        const uint8_t scalefactor = ch.scalefactors[i];

        if (scalefactor > 0) {
            const int noise_level = ath_curve[i] + ((packed_noise_level + static_cast<int>(i)) >> 8);
            const int curve_position = noise_level + 1 - ((5 * scalefactor) >> 1);
            if (curve_position < 0) {
                new_resolution = 15;
            } else if (curve_position <= 65) {
                new_resolution = tables::RESOLUTION_INVERT_TABLE[curve_position];
            } else {
                new_resolution = 0;
            }

            if (new_resolution > max_res) {
                new_resolution = static_cast<uint8_t>(max_res);
            } else if (new_resolution < min_res) {
                new_resolution = static_cast<uint8_t>(min_res);
            }
        }

        if (new_resolution < 1) {
            ch.noises[noise_count++] = static_cast<uint8_t>(i);
        } else {
            ch.noises[HCA_SAMPLES_PER_SUBFRAME - 1 - valid_count++] = static_cast<uint8_t>(i);
        }

        ch.resolution[i] = new_resolution;
    }

    ch.noise_count = static_cast<uint8_t>(noise_count);
    ch.valid_count = static_cast<uint8_t>(valid_count);
    std::fill(ch.resolution.begin() + ch.coded_count, ch.resolution.end(), uint8_t{0});
}

void calculate_gain(DecodeChannel& ch) {
    for (uint32_t i = 0; i < ch.coded_count; ++i) {
        ch.gain[i] = dequantizer_scale(ch.scalefactors[i]) * quantizer_step_size(ch.resolution[i]);
    }
}

void dequantize_coefficients(DecodeChannel& ch, BitReader& br, int subframe) {
    for (uint32_t i = 0; i < ch.coded_count; ++i) {
        const uint8_t resolution = ch.resolution[i];
        const uint8_t bits = tables::QUANTIZED_SPECTRUM_MAX_BITS[resolution];
        const uint32_t code = br.read(bits);

        float quantized = 0.0f;
        if (resolution > 7) {
            const int signed_code = (1 - static_cast<int>((code & 1) << 1)) * static_cast<int>(code >> 1);
            if (signed_code == 0) {
                br.skip(-1);
            }
            quantized = static_cast<float>(signed_code);
        } else {
            const int index = (resolution << 4) + static_cast<int>(code);
            const int skip = static_cast<int>(tables::QUANTIZED_SPECTRUM_DECODE_TABLES.read_bits[index]) - bits;
            br.skip(skip);
            quantized = static_cast<float>(tables::QUANTIZED_SPECTRUM_DECODE_TABLES.values[index]);
        }

        ch.spectra[subframe][i] = ch.gain[i] * quantized;
    }

    std::fill(ch.spectra[subframe].begin() + ch.coded_count, ch.spectra[subframe].end(), 0.0f);
}

void reconstruct_noise(DecodeChannel& ch, int min_res, bool ms_stereo, uint32_t& random, int subframe) {
    if (min_res > 0 || ch.valid_count == 0 || ch.noise_count == 0) {
        return;
    }
    if (ms_stereo && ch.type != ChannelType::StereoPrimary) {
        return;
    }

    for (uint8_t i = 0; i < ch.noise_count; ++i) {
        random = 0x343FDu * random + 0x269EC3u;
        const int random_index = HCA_SAMPLES_PER_SUBFRAME - ch.valid_count +
            static_cast<int>(((random & 0x7FFFu) * ch.valid_count) >> 15);
        const int noise_index = ch.noises[i];
        const int valid_index = ch.noises[random_index];
        const int sf_noise = ch.scalefactors[noise_index];
        const int sf_valid = ch.scalefactors[valid_index];
        const int scale_index = std::max(0, sf_noise - sf_valid + 62);
        ch.spectra[subframe][noise_index] = scale_conversion(scale_index) * ch.spectra[subframe][valid_index];
    }
}

void reconstruct_hfr(DecodeChannel& ch, const HcaHeader& info, int subframe) {
    if (info.codec.bands_per_hfr_group == 0 || ch.type == ChannelType::StereoSecondary) {
        return;
    }

    const int total_band_count = std::min(static_cast<int>(info.codec.total_band_count), 127);
    const int start_band = info.codec.base_band_count + info.codec.stereo_band_count;
    int high_band = start_band;
    int low_band = start_band - 1;
    const int group_limit = info.file.version <= HCA_VERSION_V200
        ? info.codec.hfr_group_count
        : (info.codec.hfr_group_count >> 1);

    for (int group = 0; group < info.codec.hfr_group_count; ++group) {
        const int low_band_step = group < group_limit ? 1 : 0;
        for (int i = 0; i < info.codec.bands_per_hfr_group; ++i) {
            if (high_band >= total_band_count || low_band < 0) {
                break;
            }

            const int scale_index = std::max(0, static_cast<int>(ch.hfr_scales[group]) - ch.scalefactors[low_band] + 63);
            ch.spectra[subframe][high_band] = scale_conversion(scale_index) * ch.spectra[subframe][low_band];
            ++high_band;
            low_band -= low_band_step;
        }
    }

    if (high_band > 0) {
        ch.spectra[subframe][high_band - 1] = 0.0f;
    }
}

void apply_intensity_stereo(DecodeChannel* ch_pair, int subframe, int base_band, int total_band) {
    if (ch_pair[0].type != ChannelType::StereoPrimary) {
        return;
    }

    const float ratio_l = tables::INTENSITY_RATIO_TABLE[ch_pair[1].intensity[subframe]];
    const float ratio_r = 2.0f - ratio_l;
    for (int band = base_band; band < total_band; ++band) {
        const float coefficient = ch_pair[0].spectra[subframe][band];
        ch_pair[1].spectra[subframe][band] = coefficient * ratio_r;
        ch_pair[0].spectra[subframe][band] = coefficient * ratio_l;
    }
}

void apply_ms_stereo(DecodeChannel* ch_pair, bool ms_stereo, int base_band, int total_band, int subframe) {
    if (!ms_stereo || ch_pair[0].type != ChannelType::StereoPrimary) {
        return;
    }

    constexpr float ratio = 0.70710676908493f;
    for (int band = base_band; band < total_band; ++band) {
        const float left = ch_pair[0].spectra[subframe][band];
        const float right = ch_pair[1].spectra[subframe][band];
        ch_pair[0].spectra[subframe][band] = (left + right) * ratio;
        ch_pair[1].spectra[subframe][band] = (left - right) * ratio;
    }
}

void imdct_transform(DecodeChannel& ch, int subframe) {
    const auto& window = tables::IMDCT_WINDOW;
    const auto dct_out = transform::dct4(ch.spectra[subframe], transform::HCA_DCT4_IMDCT_SCALE);

    for (int i = 0; i < HCA_SAMPLES_PER_SUBFRAME / 2; ++i) {
        ch.wave[subframe][i] =
            window[i] * dct_out[i + (HCA_SAMPLES_PER_SUBFRAME / 2)] + ch.imdct_previous[i];
        ch.wave[subframe][i + (HCA_SAMPLES_PER_SUBFRAME / 2)] =
            window[i + (HCA_SAMPLES_PER_SUBFRAME / 2)] * dct_out[HCA_SAMPLES_PER_SUBFRAME - 1 - i] -
            ch.imdct_previous[i + (HCA_SAMPLES_PER_SUBFRAME / 2)];
        ch.imdct_previous[i] =
            window[HCA_SAMPLES_PER_SUBFRAME - 1 - i] * dct_out[(HCA_SAMPLES_PER_SUBFRAME / 2) - 1 - i];
        ch.imdct_previous[i + (HCA_SAMPLES_PER_SUBFRAME / 2)] =
            window[(HCA_SAMPLES_PER_SUBFRAME / 2) - 1 - i] * dct_out[i];
    }
}

std::expected<void, std::string> decode_frame(DecodeFrame& frame, const uint8_t* data) {
    const auto& info = frame.info;
    if (data[0] != 0xFF || data[1] != 0xFF) {
        return std::unexpected(std::string("HCA decode failed: invalid frame sync"));
    }

    BitReader br(data, info.codec.frame_size);
    br.skip(16);

    const int acceptable_noise_level = static_cast<int>(br.read(9));
    const int evaluation_boundary = static_cast<int>(br.read(7));
    const int packed_noise_level = (acceptable_noise_level << 8) - evaluation_boundary;

    for (uint32_t ch = 0; ch < info.fmt.channel_count; ++ch) {
        if (!unpack_scalefactors(frame.channels[ch], br,
                                 frame.channels[ch].type != ChannelType::StereoSecondary ? info.codec.hfr_group_count : 0,
                                 info.file.version)) {
            return std::unexpected(std::string("HCA decode failed: scalefactors are malformed"));
        }
        if (!unpack_intensity(frame.channels[ch], br, info.codec.hfr_group_count, info.file.version)) {
            return std::unexpected(std::string("HCA decode failed: intensity data is malformed"));
        }
        calculate_resolution(frame.channels[ch], packed_noise_level, frame.ath_curve.data(),
                             info.codec.min_resolution, info.codec.max_resolution);
        calculate_gain(frame.channels[ch]);
    }

    for (int subframe = 0; subframe < HCA_SUBFRAMES; ++subframe) {
        for (uint32_t ch = 0; ch < info.fmt.channel_count; ++ch) {
            dequantize_coefficients(frame.channels[ch], br, subframe);
        }

        for (uint32_t ch = 0; ch < info.fmt.channel_count; ++ch) {
            reconstruct_noise(frame.channels[ch], info.codec.min_resolution, info.codec.uses_ms_stereo(), frame.random, subframe);
            reconstruct_hfr(frame.channels[ch], info, subframe);
        }

        if (info.codec.stereo_band_count > 0) {
            for (uint32_t ch = 0; ch + 1 < info.fmt.channel_count; ++ch) {
                apply_intensity_stereo(&frame.channels[ch], subframe, info.codec.base_band_count, info.codec.total_band_count);
                apply_ms_stereo(&frame.channels[ch], info.codec.uses_ms_stereo(), info.codec.base_band_count, info.codec.total_band_count, subframe);
            }
        }

        for (uint32_t ch = 0; ch < info.fmt.channel_count; ++ch) {
            imdct_transform(frame.channels[ch], subframe);
        }
    }

    return {};
}

void write_samples(const DecodeFrame& frame, uint32_t frame_offset, uint32_t sample_count, int16_t* output) {
    const auto& info = frame.info;
    uint32_t sample_index = frame_offset;
    uint32_t remaining = sample_count;
    while (remaining > 0) {
        const uint32_t subframe = sample_index / HCA_SAMPLES_PER_SUBFRAME;
        const uint32_t first_sample = sample_index % HCA_SAMPLES_PER_SUBFRAME;
        const uint32_t samples_this_subframe =
            std::min<uint32_t>(HCA_SAMPLES_PER_SUBFRAME - first_sample, remaining);
        const uint32_t sample_end = first_sample + samples_this_subframe;

        for (uint32_t sample = first_sample; sample < sample_end; ++sample) {
            for (uint32_t ch = 0; ch < info.fmt.channel_count; ++ch) {
                float value = frame.channels[ch].wave[subframe][sample] * 32768.0f;
                value = cricodecs::util::clamp(value, -32768.0f, 32767.0f);
                *output++ = static_cast<int16_t>(value);
            }
        }

        sample_index += samples_this_subframe;
        remaining -= samples_this_subframe;
    }
}

} // namespace

std::expected<std::vector<int16_t>, std::string> decode(
    std::span<const uint8_t> hca_data, uint64_t keycode, uint16_t subkey) {
    auto info_result = detail::parse_header(hca_data);
    if (!info_result) {
        return std::unexpected(info_result.error());
    }

    DecodeFrame frame{.info = *info_result};
    const HcaHeader& info = frame.info;
    assign_channel_types(frame);

    if (info.ath.uses_curve()) {
        tables::scale_ath_curve(info.fmt.sample_rate, frame.ath_curve);
    } else {
        frame.ath_curve.fill(0);
    }

    std::array<uint8_t, 256> cipher_table{};
    cipher::init_cipher(cipher_table, info.cipher.type, keycode, subkey);

    const uint32_t available_frames = info.available_frame_count(hca_data.size());
    if (available_frames == 0) {
        return std::unexpected(std::string("HCA decode failed: no complete frames are available"));
    }

    const uint32_t sample_count = info.sample_count_for_frames(available_frames);
    const size_t output_sample_count =
        static_cast<size_t>(sample_count) * info.fmt.channel_count;
    std::vector<int16_t> output(output_sample_count);
    std::vector<uint8_t> frame_buffer(info.codec.frame_size);

    const uint8_t* frame_data = hca_data.data() + info.file.header_size;
    uint32_t samples_written = 0;
    uint32_t samples_to_skip = info.fmt.encoder_delay;

    for (uint32_t frame_index = 0; frame_index < available_frames; ++frame_index) {
        if (samples_written >= sample_count) {
            break;
        }
        const size_t frame_byte_offset =
            static_cast<size_t>(frame_index) * info.codec.frame_size;
        std::memcpy(frame_buffer.data(), frame_data + frame_byte_offset, info.codec.frame_size);
        if (tables::crc16_checksum(frame_buffer.data(), info.codec.frame_size - 2) !=
            read_be<uint16_t>(frame_buffer.data() + info.codec.frame_size - 2)) {
            return std::unexpected(std::string("HCA decode failed: frame checksum mismatch"));
        }

        cipher::transform_frame(cipher_table, frame_buffer);
        const auto decode_result = decode_frame(frame, frame_buffer.data());
        if (!decode_result) {
            return std::unexpected(decode_result.error());
        }

        uint32_t frame_offset = 0;
        uint32_t samples_in_frame = HCA_SAMPLES_PER_FRAME;
        if (samples_to_skip > 0) {
            const uint32_t skip = std::min(samples_to_skip, samples_in_frame);
            frame_offset = skip;
            samples_in_frame -= skip;
            samples_to_skip -= skip;
        }

        const uint32_t samples_to_copy = std::min(samples_in_frame, sample_count - samples_written);
        write_samples(
            frame,
            frame_offset,
            samples_to_copy,
            output.data() + static_cast<size_t>(samples_written) * info.fmt.channel_count
        );
        samples_written += samples_to_copy;
    }

    return output;
}

static std::expected<std::vector<uint8_t>, std::string> encrypt_copy(
    std::span<const uint8_t> hca_data,
    const HcaHeader& info,
    uint16_t cipher_type,
    uint64_t keycode,
    uint16_t subkey) {
    std::vector<uint8_t> output(hca_data.begin(), hca_data.end());
    auto crypt_result = detail::encrypt_in_place(output, info, cipher_type, keycode, subkey);
    if (!crypt_result) {
        return std::unexpected(crypt_result.error());
    }
    return output;
}

static std::expected<std::vector<uint8_t>, std::string> decrypt_copy(
    std::span<const uint8_t> hca_data,
    const HcaHeader& info,
    uint64_t keycode,
    uint16_t subkey) {
    std::vector<uint8_t> output(hca_data.begin(), hca_data.end());
    auto crypt_result = detail::decrypt_in_place(output, info, keycode, subkey);
    if (!crypt_result) {
        return std::unexpected(crypt_result.error());
    }
    return output;
}

std::expected<std::vector<uint8_t>, std::string> encrypt(
    std::span<const uint8_t> hca_data,
    uint16_t cipher_type,
    uint64_t keycode,
    uint16_t subkey) {
    auto info_result = detail::parse_header(hca_data);
    if (!info_result) {
        return std::unexpected(info_result.error());
    }

    if (info_result->cipher.type != 0) {
        return std::unexpected(std::string("HCA encrypt failed: input is already encrypted"));
    }
    if (cipher_type != 1 && cipher_type != 56) {
        return std::unexpected(std::string("HCA encrypt failed: unsupported target cipher type"));
    }

    return encrypt_copy(hca_data, *info_result, cipher_type, keycode, subkey);
}

std::expected<std::vector<uint8_t>, std::string> decrypt(
    std::span<const uint8_t> hca_data,
    uint64_t keycode,
    uint16_t subkey) {
    auto info_result = detail::parse_header(hca_data);
    if (!info_result) {
        return std::unexpected(info_result.error());
    }

    if (info_result->cipher.type == 0) {
        return std::unexpected(std::string("HCA decrypt failed: input is not encrypted"));
    }

    return decrypt_copy(hca_data, *info_result, keycode, subkey);
}

} // namespace cricodecs::hca
