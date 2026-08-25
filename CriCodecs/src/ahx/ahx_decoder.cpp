/**
 * @file ahx_decoder.cpp
 * @brief AHX frame parser and MPEG Layer II-style synthesis path.
 *
 * The first in-tree decoder pass was based on radx. This implementation has
 * since been reworked against CRI ahxencd evidence: MPEG header field handling,
 * frame allocation parsing, subband synthesis naming, and fixed-point tables.
 *
 * Attribution:
 * - Initial AHX decode reference: radx.
 * - Current behavior checks: CRI ahxencd.
 * - Codec model: MPEG Layer II subband synthesis.
 * - CriCodecs C++23 port and reverse-engineering follow-up by Youjose.
 *
 * TODO(ahx): The synthesis hotspot still uses the direct MPEG reference
 * 64x30 matrix plus windowing shape. A future optimization pass should derive
 * and benchmark a DCT32/DCT64-style synthesis kernel like FFmpeg, mpg123, or
 * libmad, while preserving AHX fixed-point rounding.
 */

#include "ahx_codec.hpp"
#include "ahx_format.hpp"
#include "ahx_signal_tables.hpp"
#include "ahx_allocation_tables.hpp"

#include <algorithm>
#include <cstring>

#include "../utilities/numeric.hpp"

namespace cricodecs::ahx {

namespace {

using detail::AHX_BITALLOC_TABLE;
using detail::AHX_BANDS;
using detail::AHX_DECODE_QUANT_TABLE_HIGH;
using detail::AHX_DECODE_QUANT_TABLE_LOW;
using detail::AHX_SBF_SYNTHESIS_WINDOW_TABLE;
using detail::AHX_EXPECTED_FRAME_SIZE;
using detail::AHX_FOOTER_PREFIX;
using detail::AHX_FOOTER_TAG;
using detail::AHX_FRAC_BITS;
using detail::AHX_GRANULES;
using detail::AHX_GROUPED_3_LEVEL_DECODE_TABLE;
using detail::AHX_GROUPED_5_LEVEL_DECODE_TABLE;
using detail::AHX_GROUPED_9_LEVEL_DECODE_TABLE;
using detail::AHX_OFFSET_TABLE;
using detail::AHX_SUBBAND_FILTER_MATRIX;
using detail::AHX_QBITS_TABLE;
using detail::AHX_SAMPLES_PER_FRAME;
using detail::AHX_SF_TABLE;
using detail::AhxDecodeQuantSpec;
using detail::is_ahx_frame_header;
using detail::wrapping_add_i64;
using detail::wrapping_mul_i64;
using util::divide_round_up;

class AhxFrameDecoder {
public:
    [[nodiscard]] std::expected<std::array<int16_t, AHX_SAMPLES_PER_FRAME>, AhxError> decode_frame(
        std::span<const uint8_t, AHX_EXPECTED_FRAME_SIZE> frame_data
    ) {
        io::bit_reader reader(frame_data);
        const uint32_t frame_header = reader.read(32);
        if (!is_ahx_frame_header(frame_header)) {
            return std::unexpected(AhxError("Invalid AHX frame header"));
        }

        std::array<uint32_t, AHX_BANDS> allocations{};
        for (int band = 0; band < AHX_BANDS; ++band) {
            allocations[band] = reader.read(AHX_BITALLOC_TABLE[band]);
        }

        std::array<uint32_t, AHX_BANDS> scfsi{};
        for (int band = 0; band < AHX_BANDS; ++band) {
            if (allocations[band] != 0) {
                scfsi[band] = reader.read(2);
            }
        }

        std::array<std::array<uint32_t, 3>, AHX_BANDS> scalefactors{};
        for (int band = 0; band < AHX_BANDS; ++band) {
            if (allocations[band] == 0) {
                continue;
            }

            switch (scfsi[band]) {
                case 0:
                    scalefactors[band][0] = reader.read(6);
                    scalefactors[band][1] = reader.read(6);
                    scalefactors[band][2] = reader.read(6);
                    break;
                case 1: {
                    const uint32_t shared = reader.read(6);
                    scalefactors[band][0] = shared;
                    scalefactors[band][1] = shared;
                    scalefactors[band][2] = reader.read(6);
                    break;
                }
                case 2: {
                    const uint32_t shared = reader.read(6);
                    scalefactors[band][0] = shared;
                    scalefactors[band][1] = shared;
                    scalefactors[band][2] = shared;
                    break;
                }
                case 3:
                    scalefactors[band][0] = reader.read(6);
                    scalefactors[band][1] = reader.read(6);
                    scalefactors[band][2] = scalefactors[band][1];
                    break;
            }
        }

        std::array<int16_t, AHX_SAMPLES_PER_FRAME> pcm{};
        for (int part = 0; part < 3; ++part) {
            for (int granule = 0; granule < 4; ++granule) {
                std::array<std::array<int64_t, 3>, 32> subband_samples{};

                for (int band = 0; band < AHX_BANDS; ++band) {
                    const uint32_t allocation = allocations[band];
                    if (allocation == 0) {
                        continue;
                    }

                    const auto& quant = band < 4
                        ? AHX_DECODE_QUANT_TABLE_LOW[allocation - 1]
                        : AHX_DECODE_QUANT_TABLE_HIGH[allocation - 1];
                    const auto samples = read_samples(reader, quant);

                    const int64_t scale = AHX_SF_TABLE[scalefactors[band][part]];
                    for (int idx = 0; idx < 3; ++idx) {
                        subband_samples[band][idx] = wrapping_mul_i64(samples[idx], scale) >> AHX_FRAC_BITS;
                    }
                }

                for (int idx = 0; idx < 3; ++idx) {
                    const size_t table_index = (m_v_offset + 1024 - 64) % 1024;
                    m_v_offset = table_index;

                    // Hot path: keep this direct matrix shape until a DCT32/DCT64
                    // synthesis replacement is derived and validated for AHX.
                    for (int i = 0; i < 64; ++i) {
                        int64_t sum = 0;
                        for (int band = 0; band < AHX_BANDS; ++band) {
                            sum += wrapping_mul_i64(AHX_SUBBAND_FILTER_MATRIX[i][band], subband_samples[band][idx]) >>
                                AHX_FRAC_BITS;
                        }
                        const size_t sample_index = table_index + static_cast<size_t>(i);
                        m_v[sample_index] = sum;
                        m_v[sample_index + 1024u] = sum;
                    }

                    for (int band = 0; band < 32; ++band) {
                        int64_t sum = 0;
                        const size_t band_offset = static_cast<size_t>(band);
                        for (int phase = 0; phase < 8; ++phase) {
                            const size_t base = table_index + static_cast<size_t>(phase) * 128u + band_offset;
                            const size_t dewindow_index = static_cast<size_t>(phase) * 64u + band_offset;
                            sum -= wrapping_mul_i64(m_v[base], AHX_SBF_SYNTHESIS_WINDOW_TABLE[dewindow_index]) >>
                                AHX_FRAC_BITS;
                            sum -= wrapping_mul_i64(m_v[base + 96u], AHX_SBF_SYNTHESIS_WINDOW_TABLE[dewindow_index + 32u]) >>
                                AHX_FRAC_BITS;
                        }

                        sum >>= (AHX_FRAC_BITS - 15);
                        sum = std::clamp<int64_t>(-sum, -32768, 32767);

                        pcm[static_cast<size_t>(part * 384 + granule * 96 + idx * 32 + band)] =
                            static_cast<int16_t>(sum);
                    }
                }
            }
        }

        return pcm;
    }

private:
    [[nodiscard]] static constexpr std::array<int64_t, 3> grouped_samples(uint32_t code, uint32_t bits) noexcept {
        switch (bits) {
            case 5:
                return AHX_GROUPED_3_LEVEL_DECODE_TABLE[code];
            case 7:
                return AHX_GROUPED_5_LEVEL_DECODE_TABLE[code];
            case 10:
                return AHX_GROUPED_9_LEVEL_DECODE_TABLE[code];
            default:
                return {};
        }
    }

    [[nodiscard]] static std::array<int64_t, 3> read_samples(
        io::bit_reader& reader,
        const AhxDecodeQuantSpec& quant
    ) noexcept {
        std::array<int64_t, 3> samples{};

        if (quant.group != 0) {
            return grouped_samples(reader.read(static_cast<int>(quant.bits)), quant.bits);
        }

        const uint32_t num_bits = quant.bits;
        for (int idx = 0; idx < 3; ++idx) {
            samples[idx] = reader.read(static_cast<int>(num_bits));
        }

        for (int idx = 0; idx < 3; ++idx) {
            int64_t requantized = samples[idx] ^ (1LL << (num_bits - 1));
            requantized |= -(requantized & (1LL << (num_bits - 1)));
            requantized <<= (AHX_FRAC_BITS - (num_bits - 1));
            samples[idx] = wrapping_mul_i64(wrapping_add_i64(requantized, quant.d), quant.c) >> AHX_FRAC_BITS;
        }

        return samples;
    }

    size_t m_v_offset = 0;
    std::array<int64_t, 2048> m_v{};
};

[[nodiscard]] size_t parse_ahx_frame_bits(
    const uint8_t* input,
    uint8_t* output,
    size_t actual_size,
    const AhxKey* key
) noexcept {
    std::memcpy(output, input, actual_size);

    io::bit_reader in_bits(input, actual_size);
    io::bit_writer out_bits(output, actual_size);

    std::array<uint32_t, AHX_BANDS> bit_alloc{};
    std::array<uint32_t, AHX_BANDS> scfsi{};

    in_bits.skip(32);
    out_bits.set_position(32);

    for (int band = 0; band < AHX_BANDS; ++band) {
        const int bits = AHX_BITALLOC_TABLE[band];
        bit_alloc[band] = in_bits.read(bits);
        out_bits.set_position(out_bits.position() + static_cast<size_t>(bits));
    }

    if (bit_alloc[0] != 0) {
        scfsi[0] = in_bits.read(2);
        out_bits.set_position(out_bits.position() + 2);
    }

    uint16_t ahx_key = 0;
    if (key != nullptr) {
        switch (scfsi[0]) {
            case 1: ahx_key = key->start; break;
            case 2: ahx_key = key->mult; break;
            case 3: ahx_key = key->add; break;
            default: break;
        }
    }

    for (int band = 1; band < AHX_BANDS; ++band) {
        if (bit_alloc[band] != 0) {
            scfsi[band] = in_bits.read(2);
            if (key != nullptr) {
                scfsi[band] ^= (ahx_key & 0x03u);
            }
            out_bits.write(scfsi[band], 2);
        }
        ahx_key = static_cast<uint16_t>(ahx_key >> 2);
    }

    for (int band = 0; band < AHX_BANDS; ++band) {
        if (bit_alloc[band] == 0) {
            continue;
        }

        switch (scfsi[band]) {
            case 0: in_bits.skip(18); break;
            case 1:
            case 3: in_bits.skip(12); break;
            case 2: in_bits.skip(6); break;
            default: return 0;
        }
    }

    for (int granule = 0; granule < AHX_GRANULES; ++granule) {
        for (int band = 0; band < AHX_BANDS; ++band) {
            const int alloc_value = static_cast<int>(bit_alloc[band]);
            if (alloc_value == 0) {
                continue;
            }

            const int alloc_bits = AHX_BITALLOC_TABLE[band];
            const int qb_index = AHX_OFFSET_TABLE[alloc_bits][alloc_value - 1];
            int quant_bits = AHX_QBITS_TABLE[qb_index];
            quant_bits = quant_bits < 0 ? -quant_bits : quant_bits * 3;

            if (in_bits.remaining() < static_cast<size_t>(quant_bits)) {
                return 0;
            }
            in_bits.skip(quant_bits);
        }
    }

    const size_t bit_position = in_bits.position();
    const size_t byte_position = (bit_position + 7) / 8;
    if (byte_position > actual_size) {
        return 0;
    }

    return byte_position;
}

[[nodiscard]] size_t find_ahx_frame_size(
    std::span<const uint8_t> file_data,
    size_t offset,
    const AhxKey* key,
    std::span<uint8_t, AHX_EXPECTED_FRAME_SIZE> parsed_frame
) noexcept {
    // AHX inputs can strip frame padding, so scan for the next valid MPEG
    // Layer II mono header or CRI footer instead of assuming a fixed 0x414 span.
    // While validating the boundary, also leave the normalized frame in
    // parsed_frame so decode does not have to parse the same bitstream twice.
    if (offset + 4 > file_data.size()) {
        return 0;
    }

    if (!is_ahx_frame_header(io::read_be<uint32_t>(file_data.data() + offset))) {
        return 0;
    }

    const size_t max_scan = std::min(file_data.size(), offset + AHX_EXPECTED_FRAME_SIZE + 4);
    for (size_t pos = offset + 4; pos < max_scan; ++pos) {
        bool is_boundary = false;
        if (pos + 4 <= file_data.size()) {
            const uint32_t marker = io::read_be<uint32_t>(file_data.data() + pos);
            is_boundary = (is_ahx_frame_header(marker) || marker == AHX_FOOTER_PREFIX || marker == AHX_FOOTER_TAG);
        }
        if (!is_boundary) {
            continue;
        }

        const size_t candidate_size = pos - offset;
        if (candidate_size == 0 || candidate_size > AHX_EXPECTED_FRAME_SIZE) {
            continue;
        }

        std::fill(parsed_frame.begin(), parsed_frame.end(), uint8_t{0});
        if (parse_ahx_frame_bits(file_data.data() + offset, parsed_frame.data(), candidate_size, key) != 0) {
            return candidate_size;
        }
    }

    if (offset + AHX_EXPECTED_FRAME_SIZE >= file_data.size()) {
        const size_t candidate_size = file_data.size() - offset;
        if (candidate_size <= AHX_EXPECTED_FRAME_SIZE &&
            (std::fill(parsed_frame.begin(), parsed_frame.end(), uint8_t{0}),
             parse_ahx_frame_bits(file_data.data() + offset, parsed_frame.data(), candidate_size, key) != 0)) {
            return candidate_size;
        }
    }

    return 0;
}

[[nodiscard]] std::expected<const AhxKey*, AhxError> frame_key(
    std::span<const uint8_t> file_data,
    const AhxDecodeConfig& config
) {
    if (config.channels != 1 || config.start_offset >= file_data.size() ||
        (config.encoding_mode != 0x10 && config.encoding_mode != 0x11)) {
        return std::unexpected(AhxError("Invalid AHX header"));
    }
    if (config.encryption_type != 0 && config.encryption_type != 8 &&
        config.encryption_type != 9) {
        return std::unexpected(AhxError("Unsupported AHX encryption"));
    }
    if (config.encryption_type == 0) return nullptr;
    if (config.key.empty()) {
        return std::unexpected(AhxError("AHX decryption key required"));
    }
    return &config.key;
}

template <typename Visit>
std::expected<void, AhxError> visit_frames(
    std::span<const uint8_t> file_data,
    size_t start_offset,
    const AhxKey* key,
    Visit visit
) {
    std::array<uint8_t, AHX_EXPECTED_FRAME_SIZE> frame{};
    for (size_t offset = start_offset; offset + 4 <= file_data.size();) {
        const uint32_t marker = io::read_be<uint32_t>(file_data.data() + offset);
        if (marker == AHX_FOOTER_PREFIX || marker == AHX_FOOTER_TAG) break;
        if (!is_ahx_frame_header(marker)) {
            return std::unexpected(AhxError("Invalid AHX frame header"));
        }

        const size_t size = find_ahx_frame_size(file_data, offset, key, frame);
        if (size == 0 || offset + size > file_data.size()) {
            return std::unexpected(AhxError("Failed to determine AHX frame size"));
        }
        if (auto result = visit(frame, size, offset); !result) return result;
        offset += size;
    }
    return {};
}

} // namespace

std::expected<std::vector<int16_t>, AhxError> decode(
    std::span<const uint8_t> file_data,
    const AhxDecodeConfig& config
) {
    auto key = frame_key(file_data, config);
    if (!key) return std::unexpected(key.error());

    // Both known AHX outer modes decode with the same final mono polarity convention.
    // Earlier we only flipped Dreamcast 0x10, but external 0x11 references from CRI/vgmstream
    // show the same inversion is needed there as well.
    AhxFrameDecoder frame_decoder;
    std::vector<int16_t> decoded_pcm;
    // AHX inputs can end with variable (stripped) frame padding,
    // so reserve by rounded-up frame count to avoid realloc churn while decoding.
    decoded_pcm.reserve(
        divide_round_up(file_data.size() - config.start_offset, AHX_EXPECTED_FRAME_SIZE) * AHX_SAMPLES_PER_FRAME
    );
    auto frames = visit_frames(file_data, config.start_offset, *key,
        [&](std::span<const uint8_t, AHX_EXPECTED_FRAME_SIZE> frame, size_t, size_t) -> std::expected<void, AhxError> {
        auto frame_pcm = frame_decoder.decode_frame(frame);
        if (!frame_pcm) return std::unexpected(frame_pcm.error());
        decoded_pcm.insert(decoded_pcm.end(), frame_pcm->begin(), frame_pcm->end());
        return {};
    });
    if (!frames) return std::unexpected(frames.error());
    if (decoded_pcm.size() < config.sample_count) {
        return std::unexpected(AhxError("Decoded AHX sample count was smaller than the header sample count"));
    }

    decoded_pcm.resize(config.sample_count);
    return decoded_pcm;
}

std::expected<std::vector<uint8_t>, AhxError> decrypt(
    std::span<const uint8_t> file_data,
    const AhxDecodeConfig& config
) {
    auto key = frame_key(file_data, config);
    if (!key) return std::unexpected(key.error());
    if (*key == nullptr) return std::vector<uint8_t>(file_data.begin(), file_data.end());

    std::vector<uint8_t> output(file_data.begin(), file_data.end());
    auto frames = visit_frames(file_data, config.start_offset, *key,
        [&](std::span<const uint8_t, AHX_EXPECTED_FRAME_SIZE> frame, size_t size, size_t offset) -> std::expected<void, AhxError> {
        std::ranges::copy(frame.first(size), output.begin() + static_cast<std::ptrdiff_t>(offset));
        return {};
    });
    if (!frames) return std::unexpected(frames.error());

    return output;
}

} // namespace cricodecs::ahx
