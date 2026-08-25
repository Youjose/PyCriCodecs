/**
 * @file hca_reader.cpp
 * @brief HCA header parser.
 *
 * Header layout behavior is based on vgmstream/VGAudio readers and
 * cross-checked against CRI SDK parser/serializer paths.
 */

#include "hca_reader.hpp"

#include "hca_codec.hpp"
#include "hca_format.hpp"
#include "hca_tables.hpp"

#include <limits>
#include <optional>

#include "../utilities/io_endian.hpp"
#include "../utilities/io_reader.hpp"
#include "../utilities/numeric.hpp"

namespace cricodecs::hca {

namespace {

using cricodecs::util::divide_round_up;
using io::read_be;

template <typename T>
[[nodiscard]] std::optional<T> read_chunk(io::reader& reader, uint32_t id) noexcept {
    if (reader.remaining() < sizeof(T)
        || (reader.read_be_at<uint32_t>(reader.tell()) & HCA_MASK) != id) {
        return std::nullopt;
    }
    return reader.read_be<T>();
}

[[nodiscard]] bool begin_short_chunk(io::reader& reader, uint32_t id, size_t size) noexcept {
    if (reader.remaining() < size
        || (reader.read_be_at<uint32_t>(reader.tell()) & HCA_MASK) != id) {
        return false;
    }
    reader.skip(sizeof(uint32_t));
    return true;
}

} // namespace

std::expected<HcaHeader, std::string> detail::parse_header(std::span<const uint8_t> data) {
    if (data.size() < 8) {
        return std::unexpected(std::string("HCA parse failed: header is shorter than 8 bytes"));
    }

    const auto file = read_be<detail::HcaFileRecord>(data.data());
    if ((file.id & HCA_MASK) != HCA_CHUNK_ID_HCA) {
        return std::unexpected(std::string("HCA parse failed: invalid HCA signature"));
    }

    HcaHeader info{.file = {file.version, file.header_size}};
    if (info.file.header_size < 8 || data.size() < info.file.header_size) {
        return std::unexpected(std::string("HCA parse failed: invalid header size"));
    }

    if (tables::crc16_checksum(data.data(), info.file.header_size - 2) !=
        read_be<uint16_t>(data.data() + info.file.header_size - 2)) {
        return std::unexpected(std::string("HCA parse failed: header checksum mismatch"));
    }

    io::reader reader;
    static_cast<void>(reader.open(data.first(info.file.header_size)));
    reader.seek(sizeof(file));

    const auto format = read_chunk<detail::HcaFormatRecord>(reader, HCA_CHUNK_ID_FMT);
    if (!format) {
        return std::unexpected(std::string("HCA parse failed: missing fmt chunk"));
    }
    info.fmt = {
        .channel_count = static_cast<uint8_t>(format->channel_rate >> 24),
        .sample_rate = format->channel_rate & 0x00FF'FFFFu,
        .frame_count = format->frame_count,
        .encoder_delay = format->encoder_delay,
        .encoder_padding = format->encoder_padding,
    };

    if (info.fmt.channel_count == 0 || info.fmt.channel_count > 8 || info.fmt.sample_rate == 0 || info.fmt.frame_count == 0) {
        return std::unexpected(std::string("HCA parse failed: invalid fmt chunk values"));
    }
    const uint64_t frame_sample_total = static_cast<uint64_t>(info.fmt.frame_count) * HCA_SAMPLES_PER_FRAME;
    const uint64_t sample_trim = static_cast<uint64_t>(info.fmt.encoder_delay) + info.fmt.encoder_padding;
    if (sample_trim >= frame_sample_total) {
        return std::unexpected(std::string("HCA parse failed: invalid encoder delay/padding"));
    }
    if (frame_sample_total - sample_trim > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected(std::string("HCA parse failed: sample count exceeds supported range"));
    }

    if (const auto comp = read_chunk<detail::HcaCompRecord>(reader, HCA_CHUNK_ID_COMP)) {
        info.codec.set_type(HcaCodecChunkType::Comp);
        info.codec.frame_size = comp->frame_size;
        info.codec.min_resolution = comp->min_resolution;
        info.codec.max_resolution = comp->max_resolution;
        info.codec.track_count = comp->track_count;
        info.codec.channel_config = comp->channel_config;
        info.codec.total_band_count = comp->total_band_count;
        info.codec.base_band_count = comp->base_band_count;
        info.codec.stereo_band_count = comp->stereo_band_count;
        info.codec.bands_per_hfr_group = comp->bands_per_hfr_group;
        info.codec.set_ms_stereo(comp->ms_stereo != 0);
    } else if (const auto dec = read_chunk<detail::HcaDecRecord>(reader, HCA_CHUNK_ID_DEC)) {
        info.codec.set_type(HcaCodecChunkType::Dec);
        info.codec.frame_size = dec->frame_size;
        info.codec.min_resolution = dec->min_resolution;
        info.codec.max_resolution = dec->max_resolution;
        info.codec.total_band_count = static_cast<uint8_t>(dec->total_band_count_minus_one + 1);
        info.codec.base_band_count = static_cast<uint8_t>(dec->base_band_count_minus_one + 1);
        info.codec.track_count = dec->track_config >> 4;
        info.codec.channel_config = dec->track_config & 0x0F;
        if (dec->stereo_type == 0) {
            info.codec.base_band_count = info.codec.total_band_count;
        }
        info.codec.stereo_band_count = static_cast<uint8_t>(info.codec.total_band_count - info.codec.base_band_count);
        info.codec.bands_per_hfr_group = 0;
        info.codec.set_ms_stereo(false);
    } else {
        return std::unexpected(std::string("HCA parse failed: missing comp/dec chunk"));
    }

    if (const auto vbr = read_chunk<detail::HcaVbrRecord>(reader, HCA_CHUNK_ID_VBR)) {
        info.vbr = {vbr->max_frame_size, vbr->noise_level};
        if (!(info.codec.frame_size == 0 && info.vbr.max_frame_size > HCA_MIN_FRAME_SIZE && info.vbr.max_frame_size <= 0x1FF)) {
            return std::unexpected(std::string("HCA parse failed: invalid vbr chunk"));
        }
    }

    if (begin_short_chunk(reader, HCA_CHUNK_ID_ATH, 0x06)) {
        info.ath = reader.read_be<HcaAthChunk>();
    } else {
        info.ath.type = detail::default_ath_enabled_for_missing_chunk(info.file.version) ? 1u : 0u;
    }

    if (const auto loop = read_chunk<detail::HcaLoopRecord>(reader, HCA_CHUNK_ID_LOOP)) {
        info.loop = {loop->start_frame, loop->end_frame, loop->start_delay, loop->end_padding};
        if (info.loop.start_frame > info.loop.end_frame ||
            info.loop.end_frame >= info.fmt.frame_count ||
            info.loop.start_delay >= HCA_SAMPLES_PER_FRAME ||
            info.loop.end_padding >= HCA_SAMPLES_PER_FRAME) {
            return std::unexpected(std::string("HCA parse failed: invalid loop chunk"));
        }
    }

    if (begin_short_chunk(reader, HCA_CHUNK_ID_CIPH, 0x06)) {
        info.cipher = reader.read_be<HcaCipherChunk>();
        if (info.cipher.type != 0 && info.cipher.type != 1 && info.cipher.type != 56) {
            return std::unexpected(std::string("HCA parse failed: unsupported cipher type"));
        }
    }

    if (const auto rva = read_chunk<detail::HcaRvaRecord>(reader, HCA_CHUNK_ID_RVA)) {
        info.rva.volume = rva->volume;
    }

    if (begin_short_chunk(reader, HCA_CHUNK_ID_COMM, 0x05)) {
        info.comment.length = reader.read_be<uint8_t>();
        if (reader.remaining() < info.comment.length) {
            return std::unexpected(std::string("HCA parse failed: comment chunk exceeds header"));
        }
        reader.skip(info.comment.length);
    }

    if (info.codec.frame_size < HCA_MIN_FRAME_SIZE || info.codec.frame_size > HCA_MAX_FRAME_SIZE) {
        return std::unexpected(std::string("HCA parse failed: invalid frame size"));
    }

    if (info.file.version <= HCA_VERSION_V200) {
        if (info.codec.min_resolution != 1 || info.codec.max_resolution != 15) {
            return std::unexpected(std::string("HCA parse failed: invalid v1/v2 resolution bounds"));
        }
    } else if (info.codec.min_resolution > info.codec.max_resolution || info.codec.max_resolution > 15) {
        return std::unexpected(std::string("HCA parse failed: invalid resolution bounds"));
    }

    if (info.codec.track_count == 0) {
        info.codec.track_count = 1;
    }
    if (info.codec.track_count > info.fmt.channel_count) {
        return std::unexpected(std::string("HCA parse failed: track count exceeds channel count"));
    }
    if (info.codec.base_band_count + info.codec.stereo_band_count > HCA_SAMPLES_PER_SUBFRAME ||
        info.codec.total_band_count > HCA_SAMPLES_PER_SUBFRAME ||
        info.codec.bands_per_hfr_group > HCA_SAMPLES_PER_SUBFRAME) {
        return std::unexpected(std::string("HCA parse failed: invalid band counts"));
    }

    if (info.codec.bands_per_hfr_group > 0 && info.codec.total_band_count >= info.codec.base_band_count + info.codec.stereo_band_count) {
        const uint32_t hfr_band_count = info.codec.total_band_count - info.codec.base_band_count - info.codec.stereo_band_count;
        info.codec.hfr_group_count = static_cast<uint8_t>(
            divide_round_up(hfr_band_count, static_cast<uint32_t>(info.codec.bands_per_hfr_group))
        );
    }

    return info;
}

std::expected<HcaHeader, std::string> Hca::parse_header(std::span<const uint8_t> data) {
    return detail::parse_header(data);
}

} // namespace cricodecs::hca
