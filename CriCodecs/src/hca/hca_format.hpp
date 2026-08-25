#pragma once
/**
 * @file hca_format.hpp
 * @brief HCA format layout constants and version-specific layout predicates.
 */

#include <cstdint>

#include "../utilities/io_endian.hpp"

namespace cricodecs::hca {

inline constexpr int HCA_MASK             = 0x7F7F7F7F;
inline constexpr int HCA_SUBFRAMES        = 8;
inline constexpr int HCA_SAMPLES_PER_SUBFRAME = 128;
inline constexpr int HCA_SAMPLES_PER_FRAME    = HCA_SUBFRAMES * HCA_SAMPLES_PER_SUBFRAME; // 1024
// The fmt chunk stores an eight-bit count. Ambisonics is narrower: order 14 is
// the last square count that fits (15^2 = 225).
inline constexpr int HCA_MAX_CHANNELS     = 255; // Encoder accepts all channels.
// I tried forcing 0x8F to the encoder/decoder thinking channel count has a special value (0) if so, and it failed.
inline constexpr int HCA_MAX_AMBISONICS_ORDER = 14; // 0x8E in fmt chunk.
inline constexpr int HCA_MDCT_BITS        = 7; // log2(128)
inline constexpr int HCA_MIN_FRAME_SIZE   = 8;
inline constexpr int HCA_MAX_FRAME_SIZE   = 0xFFFF;

inline constexpr uint16_t HCA_VERSION_V101 = 0x0101;
inline constexpr uint16_t HCA_VERSION_V102 = 0x0102;
inline constexpr uint16_t HCA_VERSION_V103 = 0x0103;
inline constexpr uint16_t HCA_VERSION_V200 = 0x0200;
inline constexpr uint16_t HCA_VERSION_V300 = 0x0300;

inline constexpr uint32_t HCA_CHUNK_ID_HCA  = io::FourCC{"HCA\0"}.be_value();
inline constexpr uint32_t HCA_CHUNK_ID_FMT  = io::FourCC{"fmt\0"}.be_value();
inline constexpr uint32_t HCA_CHUNK_ID_COMP = io::FourCC{"comp"}.be_value();
inline constexpr uint32_t HCA_CHUNK_ID_DEC  = io::FourCC{"dec\0"}.be_value();
inline constexpr uint32_t HCA_CHUNK_ID_VBR  = io::FourCC{"vbr\0"}.be_value();
inline constexpr uint32_t HCA_CHUNK_ID_ATH  = io::FourCC{"ath\0"}.be_value();
inline constexpr uint32_t HCA_CHUNK_ID_LOOP = io::FourCC{"loop"}.be_value();
inline constexpr uint32_t HCA_CHUNK_ID_CIPH = io::FourCC{"ciph"}.be_value();
inline constexpr uint32_t HCA_CHUNK_ID_RVA  = io::FourCC{"rva\0"}.be_value();
inline constexpr uint32_t HCA_CHUNK_ID_COMM = io::FourCC{"comm"}.be_value();
inline constexpr uint32_t HCA_CHUNK_ID_PAD  = io::FourCC{"pad\0"}.be_value();

enum class ChannelType : uint8_t {
    Discrete = 0,
    StereoPrimary = 1,
    StereoSecondary = 2,
};

namespace detail {

struct HcaFileRecord {
    uint32_t id;
    uint16_t version;
    uint16_t header_size;
};

struct HcaFormatRecord {
    uint32_t id;
    uint32_t channel_rate;
    uint32_t frame_count;
    uint16_t encoder_delay;
    uint16_t encoder_padding;
};

struct HcaCompRecord {
    uint32_t id;
    uint16_t frame_size;
    uint8_t min_resolution;
    uint8_t max_resolution;
    uint8_t track_count;
    uint8_t channel_config;
    uint8_t total_band_count;
    uint8_t base_band_count;
    uint8_t stereo_band_count;
    uint8_t bands_per_hfr_group;
    uint8_t ms_stereo;
    uint8_t reserved;
};

struct HcaDecRecord {
    uint32_t id;
    uint16_t frame_size;
    uint8_t min_resolution;
    uint8_t max_resolution;
    uint8_t total_band_count_minus_one;
    uint8_t base_band_count_minus_one;
    uint8_t track_config;
    uint8_t stereo_type;
};

struct HcaVbrRecord {
    uint32_t id;
    uint16_t max_frame_size;
    uint16_t noise_level;
};

struct HcaLoopRecord {
    uint32_t id;
    uint32_t start_frame;
    uint32_t end_frame;
    uint16_t start_delay;
    uint16_t end_padding;
};

struct HcaRvaRecord {
    uint32_t id;
    float volume;
};

[[nodiscard]] constexpr bool uses_dec_header(uint16_t version) noexcept {
    return version == HCA_VERSION_V102 || version == HCA_VERSION_V103;
}

[[nodiscard]] constexpr bool uses_comp_header(uint16_t version) noexcept {
    return version == HCA_VERSION_V200 || version == HCA_VERSION_V300;
}

[[nodiscard]] constexpr bool uses_v3_frame_layout(uint16_t version) noexcept {
    return version >= HCA_VERSION_V300;
}

[[nodiscard]] constexpr bool supports_encoder_version(uint16_t version) noexcept {
    return uses_dec_header(version) || uses_comp_header(version);
}

[[nodiscard]] constexpr bool writes_explicit_ath_chunk(uint16_t version) noexcept {
    return version == HCA_VERSION_V102 || version == HCA_VERSION_V103;
}

[[nodiscard]] constexpr uint16_t explicit_ath_type(uint16_t version, bool use_ath_curve) noexcept {
    if (writes_explicit_ath_chunk(version)) {
        return use_ath_curve ? 1u : 0u;
    }
    return 0u;
}

[[nodiscard]] constexpr bool default_ath_enabled_for_missing_chunk(uint16_t version) noexcept {
    return version < HCA_VERSION_V200;
}

} // namespace detail

} // namespace cricodecs::hca
