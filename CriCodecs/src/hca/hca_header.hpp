#pragma once
/**
 * @file hca_header.hpp
 * @brief Public HCA header chunk metadata.
 */

#include "hca_format.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cricodecs::hca {

struct HcaFileChunk {
    uint16_t version = 0;
    uint16_t header_size = 0;
};

struct HcaFmtChunk {
    uint32_t channel_count : 8 = 0;
    uint32_t sample_rate : 24 = 0;
    uint32_t frame_count = 0;
    uint16_t encoder_delay = 0;
    uint16_t encoder_padding = 0;
};

enum class HcaCodecChunkType : uint8_t {
    Unknown = 0,
    Comp,
    Dec,
};

struct HcaCodecChunk {
    uint16_t frame_size = 0;
    uint8_t min_resolution = 1;
    uint8_t max_resolution = 15;
    uint8_t track_count : 4 = 1;
    uint8_t channel_config = 0;
    uint8_t total_band_count = 0;
    uint8_t base_band_count = 0;
    uint8_t stereo_band_count = 0;
    uint8_t bands_per_hfr_group = 0;
    uint8_t hfr_group_count = 0;
    uint8_t flags = 0;

    static constexpr uint8_t ms_stereo_flag = 0x01;
    static constexpr uint8_t type_shift = 1;
    static constexpr uint8_t type_mask = 0x06;

    [[nodiscard]] HcaCodecChunkType type() const noexcept {
        return static_cast<HcaCodecChunkType>((flags & type_mask) >> type_shift);
    }

    void set_type(HcaCodecChunkType value) noexcept {
        flags = static_cast<uint8_t>((flags & ~type_mask) | ((static_cast<uint8_t>(value) << type_shift) & type_mask));
    }

    [[nodiscard]] uint8_t ms_stereo() const noexcept {
        return (flags & ms_stereo_flag) != 0 ? 1u : 0u;
    }

    void set_ms_stereo(bool enabled) noexcept {
        flags = enabled ? static_cast<uint8_t>(flags | ms_stereo_flag) : static_cast<uint8_t>(flags & ~ms_stereo_flag);
    }

    [[nodiscard]] bool uses_ms_stereo() const noexcept {
        return (flags & ms_stereo_flag) != 0;
    }
};

struct HcaVbrChunk {
    uint16_t max_frame_size = 0;
    uint16_t noise_level = 0;

    [[nodiscard]] bool enabled() const noexcept {
        return max_frame_size != 0;
    }
};

struct HcaAthChunk {
    uint16_t type = 0;

    [[nodiscard]] bool uses_curve() const noexcept {
        return type == 1;
    }
};

struct HcaLoopChunk {
    static constexpr uint32_t no_loop_frame = std::numeric_limits<uint32_t>::max();

    uint32_t start_frame = 0;
    uint32_t end_frame = no_loop_frame;
    uint16_t start_delay = 0;
    uint16_t end_padding = 0;

    [[nodiscard]] bool enabled() const noexcept {
        return end_frame != no_loop_frame;
    }
};

struct HcaCipherChunk {
    uint16_t type = 0;

    [[nodiscard]] bool encrypted() const noexcept {
        return type != 0;
    }
};

struct HcaRvaChunk {
    float volume = 1.0f;

    [[nodiscard]] bool has_volume_scale() const noexcept {
        return volume != 1.0f;
    }
};

struct HcaCommentChunk {
    uint8_t length = 0;

    [[nodiscard]] bool has_text() const noexcept {
        return length != 0;
    }
};

struct HcaHeader {
    HcaFileChunk file;
    HcaFmtChunk fmt;
    HcaCodecChunk codec;
    HcaVbrChunk vbr;
    HcaLoopChunk loop;
    HcaRvaChunk rva;
    HcaAthChunk ath;
    HcaCipherChunk cipher;
    HcaCommentChunk comment;

    [[nodiscard]] uint32_t sample_count() const noexcept {
        return sample_count_for_frames(fmt.frame_count);
    }

    [[nodiscard]] uint32_t available_frame_count(size_t byte_size) const noexcept {
        if (byte_size <= file.header_size || codec.frame_size == 0) {
            return 0;
        }
        const auto payload_bytes = static_cast<uint64_t>(byte_size - file.header_size);
        const auto complete_frames = payload_bytes / codec.frame_size;
        return static_cast<uint32_t>(std::min<uint64_t>(complete_frames, fmt.frame_count));
    }

    [[nodiscard]] uint32_t sample_count_for_frames(uint32_t frame_count) const noexcept {
        const uint64_t raw_samples = static_cast<uint64_t>(frame_count) * HCA_SAMPLES_PER_FRAME;
        const uint64_t trim = static_cast<uint64_t>(fmt.encoder_delay) + fmt.encoder_padding;
        if (raw_samples <= trim) {
            return 0;
        }
        return static_cast<uint32_t>(raw_samples - trim);
    }

    [[nodiscard]] uint32_t available_sample_count(size_t byte_size) const noexcept {
        return sample_count_for_frames(available_frame_count(byte_size));
    }
};

} // namespace cricodecs::hca
