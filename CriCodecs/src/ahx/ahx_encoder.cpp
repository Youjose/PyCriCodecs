/**
 * @file ahx_encoder.cpp
 * @brief AHX encoder path and MPEG Layer II-style analysis/quantization.
 *
 * The encoder is built around CRI ahxencd behavior where it has been mapped:
 * official default/preset .bap allocation patterns, band clamps, scalefactor
 * transmission decisions, frame allocation writes, and sample packing.
 *
 * Attribution:
 * - Encoder behavior and policy: CRI ahxencd.
 * - Codec model: MPEG Layer II subband analysis and quantization.
 * - CriCodecs C++23 implementation and reverse-engineering follow-up by
 *   Youjose.
 */

#include "ahx_codec.hpp"
#include "ahx_format.hpp"
#include "ahx_signal_tables.hpp"
#include "ahx_allocation_tables.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "../utilities/io.hpp"
#include "../utilities/numeric.hpp"

namespace cricodecs::ahx {

namespace {

using detail::AHX_BITALLOC_TABLE;
using detail::AHX_BANDS;
using detail::AHX_ENCODER_DELAY;
using detail::AHX_ENCODE_QUANT_TABLE_HIGH;
using detail::AHX_ENCODE_QUANT_TABLE_LOW;
using detail::AHX_SBA_ANALYSIS_WINDOW_TABLE;
using detail::AHX_EXPECTED_FRAME_SIZE;
using detail::AHX_FRAC_BITS;
using detail::AHX_FRAME_HEADER;
using detail::AHX_HEADER_SIZE;
using detail::AHX_ISF_TABLE;
using detail::AHX_SUBBAND_FILTER_MATRIX;
using detail::AHX_SAMPLES_PER_FRAME;
using detail::AHX_SF_TABLE;
using detail::AHX_TRANSMISSION_PATTERN_TABLE;
using detail::AhxEncodeQuantSpec;
using detail::wrapping_add_i64;
using detail::wrapping_mul_i64;
using io::append_be;
using util::divide_round_up;

class AhxWindow {
public:
    void add_samples(std::span<const int16_t, 32> samples) noexcept {
        for (size_t i = 0; i < samples.size(); ++i) {
            m_window[m_window_index + i] = samples[i];
        }
        m_window_index = (m_window_index + samples.size()) % m_window.size();
    }

    [[nodiscard]] std::array<int64_t, 32> polyphase() const noexcept {
        std::array<int64_t, 64> y{};
        std::array<int64_t, 32> polyphased{};

        for (size_t i = 0; i < y.size(); ++i) {
            for (size_t j = 0; j < 8; ++j) {
                y[i] +=
                    (static_cast<int64_t>(sample_at(i + 64 * j)) *
                     AHX_SBA_ANALYSIS_WINDOW_TABLE[i + 64 * j]) >> 15;
            }
        }

        for (size_t sb = 0; sb < polyphased.size(); ++sb) {
            for (size_t i = 0; i < y.size(); ++i) {
                polyphased[sb] += (AHX_SUBBAND_FILTER_MATRIX[i][sb] * y[i]) >> 28;
            }
        }

        return polyphased;
    }

private:
    [[nodiscard]] int16_t sample_at(size_t index) const noexcept {
        return m_window[(m_window_index + index) % m_window.size()];
    }

    std::array<int16_t, 512> m_window{};
    size_t m_window_index = 0;
};

[[nodiscard]] constexpr int classify_transmission_delta(int delta) noexcept {
    if (delta <= -3) {
        return 0;
    }
    if (delta < 0) {
        return 1;
    }
    if (delta == 0) {
        return 2;
    }
    if (delta <= 2) {
        return 3;
    }
    return 4;
}

void apply_transmission_pattern(
    std::array<std::array<int, AHX_BANDS>, 3>& scalefactors,
    std::array<int, AHX_BANDS>& scfsi
) noexcept {
    for (size_t band = 0; band < AHX_BANDS; ++band) {
        int& sf0 = scalefactors[0][band];
        int& sf1 = scalefactors[1][band];
        int& sf2 = scalefactors[2][band];

        const int delta01 = sf0 - sf1;
        const int delta12 = sf1 - sf2;
        const int pattern_index =
            classify_transmission_delta(delta01) * 5 +
            classify_transmission_delta(delta12);

        switch (AHX_TRANSMISSION_PATTERN_TABLE[static_cast<size_t>(pattern_index)]) {
            case 273:
                scfsi[band] = 2;
                sf1 = sf0;
                sf2 = sf0;
                break;
            case 275:
                scfsi[band] = 1;
                sf1 = sf0;
                break;
            case 290:
                scfsi[band] = 3;
                sf2 = sf1;
                break;
            case 291:
                scfsi[band] = 0;
                break;
            case 307:
                scfsi[band] = 3;
                sf1 = sf2;
                break;
            case 546:
                scfsi[band] = 2;
                sf0 = sf1;
                sf2 = sf1;
                break;
            case 819:
                scfsi[band] = 2;
                sf0 = sf2;
                sf1 = sf2;
                break;
            case 1092: {
                scfsi[band] = 2;
                const int shared = std::min(sf0, sf2);
                sf0 = shared;
                sf1 = shared;
                sf2 = shared;
                break;
            }
            default:
                scfsi[band] = 0;
                break;
        }
    }
}

[[nodiscard]] const AhxEncodeQuantSpec& encode_quant_spec_for_band(size_t band, uint8_t allocation) noexcept {
    return band < 4
        ? AHX_ENCODE_QUANT_TABLE_LOW[allocation - 1]
        : AHX_ENCODE_QUANT_TABLE_HIGH[allocation - 1];
}

[[nodiscard]] constexpr int64_t reconstruct_quantized_sample(const AhxEncodeQuantSpec& quant, uint32_t code) noexcept {
    const int sample_bits = quant.group != 0 ? static_cast<int>(quant.group) : static_cast<int>(quant.bits);
    int64_t requantized = static_cast<int64_t>(code) ^ (1LL << (sample_bits - 1));
    requantized |= -(requantized & (1LL << (sample_bits - 1)));
    requantized <<= (AHX_FRAC_BITS - (sample_bits - 1));
    return wrapping_mul_i64(wrapping_add_i64(requantized, quant.d), quant.c) >> AHX_FRAC_BITS;
}

[[nodiscard]] uint32_t quantize_sample(const AhxEncodeQuantSpec& quant, int64_t sample) noexcept {
    const uint32_t code_count = quant.code_count;
    if (code_count <= 1) {
        return 0;
    }

    if (sample <= quant.min_level) {
        return 0;
    }
    if (sample >= quant.max_level) {
        return code_count - 1;
    }

    int64_t approx = (sample - quant.min_level + (quant.step / 2)) / quant.step;
    approx = cricodecs::util::clamp<int64_t>(approx, 0, static_cast<int64_t>(code_count - 1));

    uint32_t best_code = static_cast<uint32_t>(approx);
    int64_t best_diff = std::numeric_limits<int64_t>::max();

    const int64_t start = std::max<int64_t>(0, approx - 2);
    const int64_t end = std::min<int64_t>(static_cast<int64_t>(code_count - 1), approx + 2);
    for (int64_t candidate = start; candidate <= end; ++candidate) {
        const int64_t reconstructed = reconstruct_quantized_sample(quant, static_cast<uint32_t>(candidate));
        const int64_t diff = reconstructed >= sample ? reconstructed - sample : sample - reconstructed;
        if (diff < best_diff) {
            best_diff = diff;
            best_code = static_cast<uint32_t>(candidate);
        }
    }

    return best_code;
}

} // namespace

std::expected<std::vector<uint8_t>, AhxError> encode(
    std::span<const int16_t> pcm_data,
    const AhxEncodeConfig& config
) {
    if (config.channels != 1 || config.sample_rate == 0) {
        return std::unexpected(AhxError("Invalid AHX encode configuration"));
    }
    if (config.encoding_mode != 0x10 && config.encoding_mode != 0x11) {
        return std::unexpected(AhxError("Unsupported AHX encoding mode"));
    }
    if (config.encryption_type != 0x00 && config.encryption_type != 0x08 && config.encryption_type != 0x09) {
        return std::unexpected(AhxError("Unsupported AHX encryption"));
    }
    if (config.encryption_type != 0x00 && config.key.empty()) {
        return std::unexpected(AhxError("AHX encryption key required"));
    }

    const AhxBitAllocationPattern bit_allocation_pattern = clamp_bit_allocation_pattern(config.bit_allocation_pattern);
    AhxWindow window;

    const auto encode_frame = [&](
        std::span<const int16_t, AHX_SAMPLES_PER_FRAME> frame_samples,
        std::vector<uint8_t>& output
    ) -> std::expected<void, AhxError> {
        const size_t frame_start = output.size();
        output.resize(frame_start + AHX_EXPECTED_FRAME_SIZE);
        io::bit_writer frame_bits(output.data() + frame_start, AHX_EXPECTED_FRAME_SIZE);

        std::array<int, AHX_BANDS> scfsi{};
        std::array<std::array<int, AHX_BANDS>, 3> scalefactors{};
        std::array<std::array<std::array<std::array<int64_t, 3>, 32>, 4>, 3> polyphased_samples{};

        frame_bits.write(AHX_FRAME_HEADER, 32);

        for (size_t band = 0; band < AHX_BANDS; ++band) {
            frame_bits.write(bit_allocation_pattern[band], AHX_BITALLOC_TABLE[band]);
        }

        size_t sample_index = 0;
        for (size_t part = 0; part < 3; ++part) {
            for (size_t granule = 0; granule < 4; ++granule) {
                for (size_t sample_group = 0; sample_group < 3; ++sample_group) {
                    std::span<const int16_t, 32> slice(frame_samples.data() + sample_index, 32);
                    window.add_samples(slice);
                    const auto polyphased = window.polyphase();
                    sample_index += 32;

                    for (size_t band = 0; band < polyphased.size(); ++band) {
                        polyphased_samples[part][granule][band][sample_group] = polyphased[band];
                    }
                }
            }

            for (size_t band = 0; band < AHX_BANDS; ++band) {
                int64_t max_sample = 0;
                for (size_t granule = 0; granule < 4; ++granule) {
                    for (size_t sample_group = 0; sample_group < 3; ++sample_group) {
                        max_sample = std::max(
                            max_sample,
                            static_cast<int64_t>(std::llabs(polyphased_samples[part][granule][band][sample_group]))
                        );
                    }
                }

                int sf_index = 62;
                while (sf_index > 0 && max_sample >= AHX_SF_TABLE[sf_index]) --sf_index;
                scalefactors[part][band] = sf_index;
            }
        }

        apply_transmission_pattern(scalefactors, scfsi);

        uint16_t frame_key = 0;
        if (bit_allocation_pattern[0] != 0) {
            switch (scfsi[0]) {
                case 1: frame_key = config.key.start; break;
                case 2: frame_key = config.key.mult; break;
                case 3: frame_key = config.key.add; break;
                default: frame_key = 0; break;
            }
        }

        if (bit_allocation_pattern[0] != 0) {
            frame_bits.write(static_cast<uint32_t>(scfsi[0]), 2);
        }
        uint16_t rolling_key = frame_key;
        for (size_t band = 1; band < AHX_BANDS; ++band) {
            if (bit_allocation_pattern[band] == 0) {
                rolling_key = static_cast<uint16_t>(rolling_key >> 2);
                continue;
            }
            uint32_t value = static_cast<uint32_t>(scfsi[band]);
            if (config.encryption_type != 0x00) {
                value ^= (rolling_key & 0x03u);
            }
            frame_bits.write(value, 2);
            rolling_key = static_cast<uint16_t>(rolling_key >> 2);
        }

        for (size_t band = 0; band < AHX_BANDS; ++band) {
            if (bit_allocation_pattern[band] == 0) {
                continue;
            }

            switch (scfsi[band]) {
                case 0:
                    frame_bits.write(static_cast<uint32_t>(scalefactors[0][band]), 6);
                    frame_bits.write(static_cast<uint32_t>(scalefactors[1][band]), 6);
                    frame_bits.write(static_cast<uint32_t>(scalefactors[2][band]), 6);
                    break;
                case 1:
                case 3:
                    frame_bits.write(static_cast<uint32_t>(scalefactors[0][band]), 6);
                    frame_bits.write(static_cast<uint32_t>(scalefactors[2][band]), 6);
                    break;
                case 2:
                    frame_bits.write(static_cast<uint32_t>(scalefactors[0][band]), 6);
                    break;
                default:
                    output.resize(frame_start);
                    return std::unexpected(AhxError("Invalid AHX scalefactor selection"));
            }
        }

        for (size_t part = 0; part < 3; ++part) {
            for (size_t granule = 0; granule < 4; ++granule) {
                for (size_t band = 0; band < AHX_BANDS; ++band) {
                    const uint8_t allocation = bit_allocation_pattern[band];
                    if (allocation == 0) {
                        continue;
                    }

                    const auto& quant = encode_quant_spec_for_band(band, allocation);
                    std::array<uint32_t, 3> quantized_samples{};

                    for (size_t sample_group = 0; sample_group < 3; ++sample_group) {
                        const int64_t scaled =
                            (polyphased_samples[part][granule][band][sample_group] *
                             AHX_ISF_TABLE[scalefactors[part][band]]) >> 28;
                        quantized_samples[sample_group] = quantize_sample(quant, scaled);
                    }

                    if (quant.group != 0) {
                        const uint32_t levels = static_cast<uint32_t>(quant.nlevels);
                        const uint32_t grouped =
                            quantized_samples[0] +
                            quantized_samples[1] * levels +
                            quantized_samples[2] * levels * levels;
                        frame_bits.write(grouped, static_cast<int>(quant.bits));
                    } else {
                        for (uint32_t sample : quantized_samples) {
                            frame_bits.write(sample, static_cast<int>(quant.bits));
                        }
                    }
                }
            }
        }

        const size_t bytes_used = (frame_bits.position() + 7) / 8;
        output.resize(frame_start + bytes_used);
        return {};
    };

    const size_t delay = config.encoding_mode == 0x11 ? AHX_ENCODER_DELAY : 0;
    const size_t input_size = delay + pcm_data.size();

    std::vector<uint8_t> encoded;
    // Final stream uses a 0x24-byte fixed header plus one 0x200-byte payload per encoded frame.
    // Reserve with divide_round_up so partial tail frames from variable-length streams still pre-allocate safely.
    encoded.reserve(AHX_HEADER_SIZE + divide_round_up(input_size, AHX_SAMPLES_PER_FRAME) * 0x200);

    append_be<uint16_t>(encoded, 0x8000);
    append_be<uint16_t>(encoded, static_cast<uint16_t>(AHX_HEADER_SIZE - 4));
    encoded.push_back(config.encoding_mode);
    encoded.push_back(0x00);
    encoded.push_back(0x00);
    encoded.push_back(config.channels);
    append_be<uint32_t>(encoded, config.sample_rate);
    append_be<uint32_t>(encoded, static_cast<uint32_t>(pcm_data.size()));
    append_be<uint16_t>(encoded, 0x0000);
    encoded.push_back(0x06);
    encoded.push_back(config.encryption_type);
    encoded.resize(AHX_HEADER_SIZE - 6);
    encoded.insert(encoded.end(), {'(', 'c', ')', 'C', 'R', 'I'});
    encoded.resize(AHX_HEADER_SIZE);

    std::array<int16_t, AHX_SAMPLES_PER_FRAME> frame_samples{};
    const size_t frame_count = std::max<size_t>(1, divide_round_up(input_size, frame_samples.size()));
    for (size_t frame = 0; frame < frame_count; ++frame) {
        frame_samples.fill(0);
        const size_t frame_start = frame * frame_samples.size();
        const size_t source_start = std::max(frame_start, delay);
        const size_t source_end = std::min(frame_start + frame_samples.size(), input_size);
        if (source_start < source_end) {
            std::ranges::copy(
                pcm_data.subspan(source_start - delay, source_end - source_start),
                frame_samples.begin() + static_cast<std::ptrdiff_t>(source_start - frame_start));
        }

        auto encoded_frame = encode_frame(std::span<const int16_t, AHX_SAMPLES_PER_FRAME>(frame_samples), encoded);
        if (!encoded_frame) {
            return std::unexpected(encoded_frame.error());
        }
    }

    encoded.insert(encoded.end(), {
        0x00, 0x80, 0x01, 0x00, 0x0C,
        'A', 'H', 'X', 'E', '(', 'c', ')', 'C', 'R', 'I', 0x00, 0x00
    });
    return encoded;
}

std::expected<std::vector<uint8_t>, AhxError> encode(
    const wav::WavContainer& wav,
    const AhxEncodeConfig& config
) {
    auto pcm = wav.get_pcm16();
    if (!pcm) {
        return std::unexpected(AhxError("AHX encode failed: ") + pcm.error());
    }

    auto effective_config = config;
    effective_config.sample_rate = wav.sample_rate();
    effective_config.channels = static_cast<uint8_t>(wav.channels());
    return encode(*pcm, effective_config);
}

} // namespace cricodecs::ahx
