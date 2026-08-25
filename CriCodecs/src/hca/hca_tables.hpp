#pragma once
/**
 * @file hca_tables.hpp
 * @brief HCA lookup tables and passive table helpers.
 *
 * Literal data is kept only where the original source is still unresolved
 * (notably ATH/IMDCT). Other tables are generated at compile time from
 * recovered rules and checked against reference values in hca_table_checks.hpp.
 */

#include "hca_format.hpp"

#include "../utilities/crc.hpp"
#include "../utilities/numeric.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cricodecs::hca {

namespace tables {

using HcaCrc16 = cricodecs::util::Crc16Msb<0x8005>;

[[nodiscard]] inline uint16_t crc16_checksum(const uint8_t* data, size_t size) noexcept {
    return HcaCrc16::checksum(data, size);
}

// ATH (Absolute Threshold of Hearing) curve - 656 entries for 41856 Hz
inline constexpr std::array<uint8_t, 656> ATH_CURVE = {
    0x78,0x5F,0x56,0x51,0x4E,0x4C,0x4B,0x49,0x48,0x48,0x47,0x46,0x46,0x45,0x45,0x45,
    0x44,0x44,0x44,0x44,0x43,0x43,0x43,0x43,0x43,0x43,0x42,0x42,0x42,0x42,0x42,0x42,
    0x42,0x42,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x40,0x40,0x40,0x40,
    0x40,0x40,0x40,0x40,0x40,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
    0x3F,0x3F,0x3F,0x3E,0x3E,0x3E,0x3E,0x3E,0x3E,0x3D,0x3D,0x3D,0x3D,0x3D,0x3D,0x3D,
    0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,
    0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,
    0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,
    0x3D,0x3D,0x3D,0x3D,0x3D,0x3D,0x3D,0x3D,0x3E,0x3E,0x3E,0x3E,0x3E,0x3E,0x3E,0x3F,
    0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
    0x3F,0x3F,0x3F,0x3F,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
    0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x41,0x41,0x41,0x41,0x41,0x41,0x41,
    0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,
    0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,
    0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x43,0x43,0x43,
    0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x44,0x44,
    0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x45,0x45,0x45,0x45,
    0x45,0x45,0x45,0x45,0x45,0x45,0x45,0x45,0x46,0x46,0x46,0x46,0x46,0x46,0x46,0x46,
    0x46,0x46,0x47,0x47,0x47,0x47,0x47,0x47,0x47,0x47,0x47,0x47,0x48,0x48,0x48,0x48,
    0x48,0x48,0x48,0x48,0x49,0x49,0x49,0x49,0x49,0x49,0x49,0x49,0x4A,0x4A,0x4A,0x4A,
    0x4A,0x4A,0x4A,0x4A,0x4B,0x4B,0x4B,0x4B,0x4B,0x4B,0x4B,0x4C,0x4C,0x4C,0x4C,0x4C,
    0x4C,0x4D,0x4D,0x4D,0x4D,0x4D,0x4D,0x4E,0x4E,0x4E,0x4E,0x4E,0x4E,0x4F,0x4F,0x4F,
    0x4F,0x4F,0x4F,0x50,0x50,0x50,0x50,0x50,0x51,0x51,0x51,0x51,0x51,0x52,0x52,0x52,
    0x52,0x52,0x53,0x53,0x53,0x53,0x54,0x54,0x54,0x54,0x54,0x55,0x55,0x55,0x55,0x56,
    0x56,0x56,0x56,0x57,0x57,0x57,0x57,0x57,0x58,0x58,0x58,0x59,0x59,0x59,0x59,0x5A,
    0x5A,0x5A,0x5A,0x5B,0x5B,0x5B,0x5B,0x5C,0x5C,0x5C,0x5D,0x5D,0x5D,0x5D,0x5E,0x5E,
    0x5E,0x5F,0x5F,0x5F,0x60,0x60,0x60,0x61,0x61,0x61,0x61,0x62,0x62,0x62,0x63,0x63,
    0x63,0x64,0x64,0x64,0x65,0x65,0x66,0x66,0x66,0x67,0x67,0x67,0x68,0x68,0x68,0x69,
    0x69,0x6A,0x6A,0x6A,0x6B,0x6B,0x6B,0x6C,0x6C,0x6D,0x6D,0x6D,0x6E,0x6E,0x6F,0x6F,
    0x70,0x70,0x70,0x71,0x71,0x72,0x72,0x73,0x73,0x73,0x74,0x74,0x75,0x75,0x76,0x76,
    0x77,0x77,0x78,0x78,0x78,0x79,0x79,0x7A,0x7A,0x7B,0x7B,0x7C,0x7C,0x7D,0x7D,0x7E,
    0x7E,0x7F,0x7F,0x80,0x80,0x81,0x81,0x82,0x83,0x83,0x84,0x84,0x85,0x85,0x86,0x86,
    0x87,0x88,0x88,0x89,0x89,0x8A,0x8A,0x8B,0x8C,0x8C,0x8D,0x8D,0x8E,0x8F,0x8F,0x90,
    0x90,0x91,0x92,0x92,0x93,0x94,0x94,0x95,0x95,0x96,0x97,0x97,0x98,0x99,0x99,0x9A,
    0x9B,0x9B,0x9C,0x9D,0x9D,0x9E,0x9F,0xA0,0xA0,0xA1,0xA2,0xA2,0xA3,0xA4,0xA5,0xA5,
    0xA6,0xA7,0xA7,0xA8,0xA9,0xAA,0xAA,0xAB,0xAC,0xAD,0xAE,0xAE,0xAF,0xB0,0xB1,0xB1,
    0xB2,0xB3,0xB4,0xB5,0xB6,0xB6,0xB7,0xB8,0xB9,0xBA,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,
    0xC0,0xC1,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xC9,0xCA,0xCB,0xCC,0xCD,
    0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,
    0xDE,0xDF,0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xED,0xEE,
    0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFF,0xFF,
};

// Scale ATH curve to target sample rate
inline void scale_ath_curve(uint32_t sample_rate, std::span<uint8_t, HCA_SAMPLES_PER_SUBFRAME> out) noexcept {
    uint32_t acc = 0;
    size_t i = 0;
    for (; i < out.size(); i++) {
        acc += sample_rate;
        uint32_t idx = acc >> 13;
        if (idx >= ATH_CURVE.size()) break;
        out[i] = ATH_CURVE[idx];
    }
    for (; i < out.size(); i++) {
        out[i] = 0xFF;
    }
}

struct QuantizedSpectrumCode {
    uint8_t bit_count = 0;
    uint16_t bits = 0;
    bool valid = false;
};

struct QuantizedSpectrumDecodeTables {
    std::array<uint8_t, 128> read_bits{};
    std::array<float, 128> values{};
};

struct QuantizedSpectrumEncodeTables {
    std::array<std::array<QuantizedSpectrumCode, 17>, 8> entries{};
};

// Ordinary defaults indexed by channels per track. Higher counts use config 0
// and remain independent unless an explicit supported mapping is requested.
inline constexpr std::array<uint8_t, 9> DEFAULT_CHANNEL_MAPPING = {0, 1, 0, 4, 0, 1, 3, 7, 3};

// Valid channel mappings [channels][config]
inline constexpr std::array<std::array<uint8_t, 8>, 8> VALID_CHANNEL_MAPPINGS = {{
    {0, 1, 0, 0, 0, 0, 0, 0},
    {1, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 0, 1, 0, 0, 0},
    {1, 0, 0, 1, 0, 1, 0, 0},
    {0, 1, 1, 0, 0, 0, 0, 1},
    {0, 0, 0, 1, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 1},
    {0, 0, 0, 1, 0, 0, 0, 0}
}};

// IMDCT window (128 entries, hex representation)
inline constexpr std::array<uint32_t, 128> IMDCT_WINDOW_HEX = {
    0x3A3504F0,0x3B0183B8,0x3B70C538,0x3BBB9268,0x3C04A809,0x3C308200,0x3C61284C,0x3C8B3F17,
    0x3CA83992,0x3CC77FBD,0x3CE91110,0x3D0677CD,0x3D198FC4,0x3D2DD35C,0x3D434643,0x3D59ECC1,
    0x3D71CBA8,0x3D85741E,0x3D92A413,0x3DA078B4,0x3DAEF522,0x3DBE1C9E,0x3DCDF27B,0x3DDE7A1D,
    0x3DEFB6ED,0x3E00D62B,0x3E0A2EDA,0x3E13E72A,0x3E1E00B1,0x3E287CF2,0x3E335D55,0x3E3EA321,
    0x3E4A4F75,0x3E56633F,0x3E62DF37,0x3E6FC3D1,0x3E7D1138,0x3E8563A2,0x3E8C72B7,0x3E93B561,
    0x3E9B2AEF,0x3EA2D26F,0x3EAAAAAB,0x3EB2B222,0x3EBAE706,0x3EC34737,0x3ECBD03D,0x3ED47F46,
    0x3EDD5128,0x3EE6425C,0x3EEF4EFF,0x3EF872D7,0x3F00D4A9,0x3F0576CA,0x3F0A1D3B,0x3F0EC548,
    0x3F136C25,0x3F180EF2,0x3F1CAAC2,0x3F213CA2,0x3F25C1A5,0x3F2A36E7,0x3F2E9998,0x3F32E705,
    0xBF371C9E,0xBF3B37FE,0xBF3F36F2,0xBF431780,0xBF46D7E6,0xBF4A76A4,0xBF4DF27C,0xBF514A6F,
    0xBF547DC5,0xBF578C03,0xBF5A74EE,0xBF5D3887,0xBF5FD707,0xBF6250DA,0xBF64A699,0xBF66D908,
    0xBF68E90E,0xBF6AD7B1,0xBF6CA611,0xBF6E5562,0xBF6FE6E7,0xBF715BEF,0xBF72B5D1,0xBF73F5E6,
    0xBF751D89,0xBF762E13,0xBF7728D7,0xBF780F20,0xBF78E234,0xBF79A34C,0xBF7A5397,0xBF7AF439,
    0xBF7B8648,0xBF7C0ACE,0xBF7C82C8,0xBF7CEF26,0xBF7D50CB,0xBF7DA88E,0xBF7DF737,0xBF7E3D86,
    0xBF7E7C2A,0xBF7EB3CC,0xBF7EE507,0xBF7F106C,0xBF7F3683,0xBF7F57CA,0xBF7F74B6,0xBF7F8DB6,
    0xBF7FA32E,0xBF7FB57B,0xBF7FC4F6,0xBF7FD1ED,0xBF7FDCAD,0xBF7FE579,0xBF7FEC90,0xBF7FF22E,
    0xBF7FF688,0xBF7FF9D0,0xBF7FFC32,0xBF7FFDDA,0xBF7FFEED,0xBF7FFF8F,0xBF7FFFDF,0xBF7FFFFC,
};

consteval std::array<float, 64> generate_dequantizer_scaling_table() {
    std::array<float, 64> table{};
    for (size_t i = 0; i < table.size(); ++i) {
        table[i] = static_cast<float>(util::exp2(3.5 + 53.0 * (static_cast<double>(i) - 63.0) / 128.0));
    }
    return table;
}

consteval std::array<float, 64> generate_quantizer_scaling_table(
    const std::array<float, 64>& dequantizer_scaling_table)
{
    std::array<float, 64> table{};
    for (size_t i = 1; i < table.size(); ++i) {
        table[i] = 1.0f / dequantizer_scaling_table[i];
    }
    return table;
}

consteval std::array<float, 16> generate_intensity_ratio_table() {
    std::array<float, 16> table{};
    for (size_t i = 0; i < table.size(); ++i) {
        table[i] = i < 15 ? static_cast<float>((28.0 - static_cast<double>(i) * 2.0) / 14.0) : 0.0f;
    }
    return table;
}

consteval std::array<float, 14> generate_intensity_ratio_bounds() {
    std::array<float, 14> table{};
    for (size_t i = 0; i < table.size(); ++i) {
        table[i] = static_cast<float>((27.0 - static_cast<double>(i) * 2.0) / 14.0);
    }
    return table;
}

consteval std::array<uint8_t, 5> generate_scalefactor_delta_limits() {
    std::array<uint8_t, 5> table{};
    for (size_t i = 0; i < table.size(); ++i) {
        table[i] = static_cast<uint8_t>((1u << i) - 1u);
    }
    return table;
}

consteval std::array<float, 128> generate_imdct_window() {
    std::array<float, IMDCT_WINDOW_HEX.size()> table{};
    for (size_t i = 0; i < table.size(); ++i) {
        table[i] = std::bit_cast<float>(IMDCT_WINDOW_HEX[i]);
    }
    return table;
}

[[nodiscard]] constexpr uint8_t quantized_spectrum_max_bits(uint8_t resolution) noexcept {
    if (resolution >= 8) {
        return static_cast<uint8_t>(resolution - 3);
    }
    return static_cast<uint8_t>(std::bit_width(static_cast<uint32_t>(2u * resolution)));
}

consteval std::array<uint8_t, 16> generate_quantized_spectrum_max_bits() {
    std::array<uint8_t, 16> table{};
    for (uint8_t resolution = 0; resolution < table.size(); ++resolution) {
        table[resolution] = quantized_spectrum_max_bits(resolution);
    }
    return table;
}

[[nodiscard]] constexpr float quantizer_inverse_step_size(uint8_t resolution) noexcept {
    if (resolution < 8) {
        return static_cast<float>(resolution) + 0.5f;
    }
    return static_cast<float>(1u << (resolution - 4)) - 0.5f;
}

[[nodiscard]] constexpr int resolution_max_value(uint8_t resolution) noexcept {
    if (resolution < 8) {
        return resolution;
    }
    return (1 << (resolution - 4)) - 1;
}

consteval std::array<float, 16> generate_quantizer_inverse_step_size_table() {
    std::array<float, 16> table{};
    for (uint8_t resolution = 0; resolution < table.size(); ++resolution) {
        table[resolution] = quantizer_inverse_step_size(resolution);
    }
    return table;
}

consteval std::array<float, 16> generate_quantizer_dead_zone_table(
    const std::array<float, 16>& inverse_step_size_table)
{
    std::array<float, 16> table{};
    for (size_t resolution = 1; resolution < table.size(); ++resolution) {
        const float boundary = 0.5f / inverse_step_size_table[resolution];
        const uint32_t bits = std::bit_cast<uint32_t>(boundary);
        table[resolution] = std::bit_cast<float>(
            bits - static_cast<uint32_t>(resolution_max_value(static_cast<uint8_t>(resolution)) + 1)
        );
    }
    return table;
}

[[nodiscard]] constexpr uint8_t resolution_for_db_position(size_t position, double reference_db) {
    const double target = util::pow10((reference_db - static_cast<double>(position)) / 20.0);
    for (uint8_t resolution = 1; resolution <= 15; ++resolution) {
        if (static_cast<double>(quantizer_inverse_step_size(resolution)) >= target) {
            return resolution;
        }
    }
    return 15;
}

consteval std::array<uint8_t, 59> generate_scale_to_resolution_curve() {
    std::array<uint8_t, 59> table{};
    for (size_t position = 0; position < table.size(); ++position) {
        table[position] = resolution_for_db_position(position, 61.0);
    }
    return table;
}

consteval std::array<uint8_t, 66> generate_resolution_invert_table() {
    std::array<uint8_t, 66> table{};
    for (size_t position = 0; position < table.size(); ++position) {
        table[position] = resolution_for_db_position(position, 60.0);
    }
    return table;
}

consteval std::array<float, 128> generate_scale_conversion_table() {
    std::array<float, 128> table{};
    for (size_t i = 0; i < table.size(); ++i) {
        table[i] = i > 0 && i < 126
            ? static_cast<float>(util::exp2(53.0 * (static_cast<double>(i) - 63.0) / 128.0))
            : 0.0f;
    }
    return table;
}

[[nodiscard]] constexpr float quantized_spectrum_symbol_value(uint8_t symbol_index) noexcept {
    if (symbol_index == 0) {
        return 0.0f;
    }

    const float magnitude = static_cast<float>((symbol_index + 1) / 2);
    return (symbol_index & 1u) != 0 ? magnitude : -magnitude;
}

consteval QuantizedSpectrumDecodeTables generate_quantized_spectrum_decode_tables() {
    QuantizedSpectrumDecodeTables tables{};
    for (uint8_t resolution = 1; resolution < 8; ++resolution) {
        const uint8_t max_bits = quantized_spectrum_max_bits(resolution);
        const uint8_t symbol_count = static_cast<uint8_t>(2 * resolution + 1);
        const uint8_t short_symbol_count = static_cast<uint8_t>((1u << max_bits) - symbol_count);
        const uint8_t short_code_count = static_cast<uint8_t>(short_symbol_count * 2);

        for (uint8_t code = 0; code < (1u << max_bits); ++code) {
            const size_t index = (static_cast<size_t>(resolution) << 4u) + code;
            uint8_t symbol_index = 0;
            if (code < short_code_count) {
                symbol_index = static_cast<uint8_t>(code >> 1u);
                tables.read_bits[index] = static_cast<uint8_t>(max_bits - 1);
            } else {
                symbol_index = static_cast<uint8_t>(short_symbol_count + code - short_code_count);
                tables.read_bits[index] = max_bits;
            }
            tables.values[index] = quantized_spectrum_symbol_value(symbol_index);
        }
    }
    return tables;
}

consteval QuantizedSpectrumEncodeTables generate_quantized_spectrum_encode_tables(
    const QuantizedSpectrumDecodeTables& decode_tables,
    const std::array<uint8_t, 16>& max_bits_table)
{
    QuantizedSpectrumEncodeTables tables{};
    for (uint8_t resolution = 1; resolution < 8; ++resolution) {
        const uint8_t max_bits = max_bits_table[resolution];
        for (uint16_t code = 0; code < (1u << max_bits); ++code) {
            const size_t decode_index = (static_cast<size_t>(resolution) << 4u) + code;
            const auto quantized = static_cast<int>(decode_tables.values[decode_index]);
            const uint8_t bit_count = decode_tables.read_bits[decode_index];
            if (bit_count == 0 || quantized < -resolution || quantized > resolution) {
                continue;
            }

            auto& entry = tables.entries[resolution][static_cast<size_t>(quantized + resolution)];
            if (entry.valid) {
                continue;
            }
            entry.bit_count = bit_count;
            entry.bits = static_cast<uint16_t>(bit_count == max_bits ? code : (code >> 1u));
            entry.valid = true;
        }
    }
    return tables;
}

inline constexpr auto DEQUANTIZER_SCALING_TABLE = generate_dequantizer_scaling_table();
inline constexpr auto QUANTIZER_SCALING_TABLE = generate_quantizer_scaling_table(DEQUANTIZER_SCALING_TABLE);
inline constexpr auto INTENSITY_RATIO_TABLE = generate_intensity_ratio_table();
inline constexpr auto INTENSITY_RATIO_BOUNDS = generate_intensity_ratio_bounds();
inline constexpr auto SCALEFACTOR_DELTA_LIMITS = generate_scalefactor_delta_limits();
inline constexpr auto IMDCT_WINDOW = generate_imdct_window();
inline constexpr auto QUANTIZED_SPECTRUM_MAX_BITS = generate_quantized_spectrum_max_bits();
inline constexpr auto QUANTIZER_INVERSE_STEP_SIZE = generate_quantizer_inverse_step_size_table();
inline constexpr auto QUANTIZER_DEAD_ZONE = generate_quantizer_dead_zone_table(QUANTIZER_INVERSE_STEP_SIZE);
inline constexpr auto SCALE_TO_RESOLUTION_CURVE = generate_scale_to_resolution_curve();
inline constexpr auto RESOLUTION_INVERT_TABLE = generate_resolution_invert_table();
inline constexpr auto SCALE_CONVERSION_TABLE = generate_scale_conversion_table();
inline constexpr auto QUANTIZED_SPECTRUM_DECODE_TABLES = generate_quantized_spectrum_decode_tables();
inline constexpr auto QUANTIZED_SPECTRUM_ENCODE_TABLES =
    generate_quantized_spectrum_encode_tables(QUANTIZED_SPECTRUM_DECODE_TABLES, QUANTIZED_SPECTRUM_MAX_BITS);

[[nodiscard]] constexpr const QuantizedSpectrumCode* quantized_spectrum_code(uint8_t resolution, int quantized) noexcept {
    if (resolution == 0 || resolution >= 8 || quantized < -resolution || quantized > resolution) {
        return nullptr;
    }

    const auto& code = QUANTIZED_SPECTRUM_ENCODE_TABLES.entries[resolution][static_cast<size_t>(quantized + resolution)];
    return code.valid ? &code : nullptr;
}

} // namespace tables


} // namespace cricodecs::hca

#include "hca_table_checks.hpp"
