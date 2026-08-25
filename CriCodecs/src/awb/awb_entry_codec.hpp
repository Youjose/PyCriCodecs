#pragma once
/**
 * @file awb_entry_codec.hpp
 * @brief Signature-based codec identification for AWB entry payloads.
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "../utilities/io_endian.hpp"

namespace cricodecs::awb {

enum class EntryCodec : uint8_t {
    Unknown,
    Hca,
    HcaMx,
    Adx,
    Ahx,
    SwLpcm,
    DsAdpcm,
    NintendoDsp,
    WiiAdpcm,
    WiiUAdpcm,
    Vag,
    Hevag,
    Atrac3,
    ThreeDsAdpcm,
    Atrac9,
    Xma2,
    SwitchOpus,
    AacM4a,
    AacAdts,
    OggVorbis,
    OggOpus,
    OggSpeex,
    Ogg,
    Wave,
    Flac,
    Mp3,
};

[[nodiscard]] inline EntryCodec probe_riff_wave_codec(
    std::span<const uint8_t> bytes) noexcept {
    if (io::read_be<uint32_t>(bytes, 0) != io::FourCC{"RIFF"}.be_value() ||
        io::read_be<uint32_t>(bytes, 8) != io::FourCC{"WAVE"}.be_value()) {
        return EntryCodec::Unknown;
    }

    size_t chunk_offset = 12;
    while (chunk_offset <= bytes.size() && bytes.size() - chunk_offset >= 8) {
        const auto chunk_size = static_cast<size_t>(
            io::read_le<uint32_t>(bytes.data() + chunk_offset + 4));
        const auto payload_offset = chunk_offset + 8;
        if (chunk_size > bytes.size() - payload_offset) {
            break;
        }
        if (io::read_be<uint32_t>(bytes, chunk_offset) == io::FourCC{"fmt "}.be_value() &&
            chunk_size >= 2) {
            const auto format = io::read_le<uint16_t>(bytes.data() + payload_offset);
            if (format == 0x0270) {
                return EntryCodec::Atrac3;
            }
            if (format == 0x0166) {
                return EntryCodec::Xma2;
            }
            static constexpr std::array<uint8_t, 16> atrac9_guid{
                0xD2, 0x42, 0xE1, 0x47, 0xBA, 0x36, 0x8D, 0x4D,
                0x88, 0xFC, 0x61, 0x65, 0x4F, 0x8C, 0x83, 0x6C};
            if (format == 0xFFFE && chunk_size >= 40 &&
                std::ranges::equal(atrac9_guid, bytes.subspan(payload_offset + 24, atrac9_guid.size()))) {
                return EntryCodec::Atrac9;
            }
            return EntryCodec::Wave;
        }

        const auto padded_size = chunk_size + (chunk_size & 1u);
        if (padded_size > bytes.size() - payload_offset) {
            break;
        }
        chunk_offset = payload_offset + padded_size;
    }
    return EntryCodec::Wave;
}

[[nodiscard]] inline EntryCodec probe_entry_codec(std::span<const uint8_t> bytes) noexcept {
    if (bytes.size() >= 4 &&
        (bytes[0] & 0x7Fu) == 'H' && (bytes[1] & 0x7Fu) == 'C' &&
        (bytes[2] & 0x7Fu) == 'A' && (bytes[3] & 0x7Fu) == 0) {
        return EntryCodec::Hca;
    }
    if (bytes.size() >= 5 && bytes[0] == 0x80 && bytes[1] == 0x00) {
        return bytes[4] == 0x10 || bytes[4] == 0x11 ? EntryCodec::Ahx : EntryCodec::Adx;
    }
    const auto magic = io::read_be<uint32_t>(bytes, 0);
    if (magic == io::FourCC{"VAGp"}.be_value()) {
        return EntryCodec::Vag;
    }
    if (magic == io::FourCC{"CWAV"}.be_value()) {
        return EntryCodec::ThreeDsAdpcm;
    }
    if (magic == io::FourCC{"CWAC"}.be_value()) {
        return EntryCodec::WiiUAdpcm;
    }
    if (magic == 0x01000080) {
        return EntryCodec::SwitchOpus;
    }
    // Raw Nintendo DSP heuristic used by vgmstream's AWB loader. The platform
    // variant is not knowable without ACB metadata; CWAC is explicitly Wii U.
    if (bytes.size() >= 0x54 &&
        io::read_be<uint32_t>(bytes, 0x08) >= 8000 &&
        io::read_be<uint32_t>(bytes, 0x08) <= 48000 &&
        io::read_be<uint16_t>(bytes, 0x0E) == 0 &&
        io::read_be<uint32_t>(bytes, 0x18) == 2 &&
        io::read_be<uint32_t>(bytes, 0x50) == 0) {
        return EntryCodec::NintendoDsp;
    }
    if (io::read_be<uint32_t>(bytes, 4) == io::FourCC{"ftyp"}.be_value()) {
        return EntryCodec::AacM4a;
    }
    if (bytes.size() >= 2 && bytes[0] == 0xFF && (bytes[1] & 0xF6u) == 0xF0u) {
        return EntryCodec::AacAdts;
    }
    if (magic == io::FourCC{"OggS"}.be_value()) {
        if (bytes.size() >= 27) {
            const auto segment_count = static_cast<size_t>(bytes[26]);
            const auto payload_offset = 27u + segment_count;
            if (payload_offset <= bytes.size()) {
                static constexpr std::array<uint8_t, 8> opus_head{
                    'O', 'p', 'u', 's', 'H', 'e', 'a', 'd'};
                static constexpr std::array<uint8_t, 6> vorbis{
                    'v', 'o', 'r', 'b', 'i', 's'};
                static constexpr std::array<uint8_t, 8> speex{
                    'S', 'p', 'e', 'e', 'x', ' ', ' ', ' '};
                if (opus_head.size() <= bytes.size() - payload_offset &&
                    std::ranges::equal(opus_head, bytes.subspan(payload_offset, opus_head.size()))) {
                    return EntryCodec::OggOpus;
                }
                if (payload_offset < bytes.size() && bytes[payload_offset] == 1 &&
                    vorbis.size() <= bytes.size() - payload_offset - 1 &&
                    std::ranges::equal(vorbis, bytes.subspan(payload_offset + 1, vorbis.size()))) {
                    return EntryCodec::OggVorbis;
                }
                if (speex.size() <= bytes.size() - payload_offset &&
                    std::ranges::equal(speex, bytes.subspan(payload_offset, speex.size()))) {
                    return EntryCodec::OggSpeex;
                }
            }
        }
        return EntryCodec::Ogg;
    }
    if (magic == io::FourCC{"RIFF"}.be_value() &&
        io::read_be<uint32_t>(bytes, 8) == io::FourCC{"WAVE"}.be_value()) {
        return probe_riff_wave_codec(bytes);
    }
    if (magic == io::FourCC{"fLaC"}.be_value()) {
        return EntryCodec::Flac;
    }
    if (bytes.size() >= 3 && bytes[0] == 'I' && bytes[1] == 'D' && bytes[2] == '3') {
        return EntryCodec::Mp3;
    }
    if (bytes.size() >= 3 && bytes[0] == 0xFF && (bytes[1] & 0xE0u) == 0xE0u) {
        const auto version = (bytes[1] >> 3) & 0x03u;
        const auto layer = (bytes[1] >> 1) & 0x03u;
        const auto bitrate = (bytes[2] >> 4) & 0x0Fu;
        const auto sample_rate = (bytes[2] >> 2) & 0x03u;
        if (version != 1 && layer != 0 && bitrate != 0 && bitrate != 0x0F && sample_rate != 3) {
            return EntryCodec::Mp3;
        }
    }
    return EntryCodec::Unknown;
}

namespace detail {

struct EntryCodecInfo {
    std::string_view name;
    std::string_view extension;
};

inline constexpr std::array entry_codec_infos{
    EntryCodecInfo{"audio", ".bin"},
    EntryCodecInfo{"HCA audio", ".hca"},
    EntryCodecInfo{"HCA-MX audio", ".hcamx"},
    EntryCodecInfo{"ADX audio", ".adx"},
    EntryCodecInfo{"AHX audio", ".ahx"},
    EntryCodecInfo{"software LPCM audio", ".swlpcm"},
    EntryCodecInfo{"Nintendo DS ADPCM audio", ".dsadpcm"},
    EntryCodecInfo{"Nintendo DSP audio", ".dsp"},
    EntryCodecInfo{"Wii ADPCM audio", ".wiiadpcm"},
    EntryCodecInfo{"Wii U ADPCM audio", ".wiiuadpcm"},
    EntryCodecInfo{"VAG audio", ".vag"},
    EntryCodecInfo{"HEVAG audio", ".vag"},
    EntryCodecInfo{"ATRAC3 audio", ".at3"},
    EntryCodecInfo{"Nintendo 3DS ADPCM audio", ".3dsadpcm"},
    EntryCodecInfo{"ATRAC9 audio", ".at9"},
    EntryCodecInfo{"XMA2 audio", ".xma2"},
    EntryCodecInfo{"Switch Opus audio", ".switchopus"},
    EntryCodecInfo{"AAC/M4A audio", ".m4a"},
    EntryCodecInfo{"AAC/ADTS audio", ".aac"},
    EntryCodecInfo{"Ogg/Vorbis audio", ".ogg"},
    EntryCodecInfo{"Ogg/Opus audio", ".opus"},
    EntryCodecInfo{"Ogg/Speex audio", ".spx"},
    EntryCodecInfo{"Ogg audio", ".ogg"},
    EntryCodecInfo{"WAV audio", ".wav"},
    EntryCodecInfo{"FLAC audio", ".flac"},
    EntryCodecInfo{"MP3 audio", ".mp3"},
};
static_assert(entry_codec_infos.size() == std::to_underlying(EntryCodec::Mp3) + 1u);

[[nodiscard]] constexpr const EntryCodecInfo& entry_codec_info(EntryCodec codec) noexcept {
    const auto index = std::to_underlying(codec);
    return index < entry_codec_infos.size() ? entry_codec_infos[index] : entry_codec_infos.front();
}

} // namespace detail

[[nodiscard]] constexpr std::string_view entry_codec_name(EntryCodec codec) noexcept {
    return detail::entry_codec_info(codec).name;
}

[[nodiscard]] constexpr std::string_view entry_codec_extension(EntryCodec codec) noexcept {
    return detail::entry_codec_info(codec).extension;
}

} // namespace cricodecs::awb
