/**
 * @file sfd_reader.cpp
 * @brief SofDec 1/SFD program-stream reader.
 *
 * Demux behavior is grounded in official SofDec samples, official stream
 * labels, and private-stream metadata recovered from the SFD tools. C++23
 * reader implementation by Youjose.
 */

#include "sfd_container.hpp"

#include <algorithm>
#include <array>
#include <sstream>

#include "../utilities/io_endian.hpp"
#include "../utilities/string.hpp"
#include "../video/mpeg.hpp"

namespace cricodecs::sfd {

namespace {

using io::read_le;
using util::trim_ascii;

constexpr std::array<uint8_t, 4> pack_start_code = {0x00, 0x00, 0x01, 0xBA};
constexpr std::array<uint8_t, 4> program_end_code = {0x00, 0x00, 0x01, 0xB9};
constexpr std::array<uint8_t, 13> legacy_sofdec_stream_preamble = {
    'S', 'o', 'f', 'd', 'e', 'c', ' ', 'S', 't', 'r', 'e', 'a', 'm'
};
constexpr std::array<uint8_t, 24> sofdec_stream_label = {
    'S', 'o', 'f', 'd', 'e', 'c', 'S', 't', 'r', 'e', 'a', 'm',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '
};
constexpr std::array<uint8_t, 24> sofdec_stream2_label = {
    'S', 'o', 'f', 'd', 'e', 'c', 'S', 't', 'r', 'e', 'a', 'm',
    '2', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '
};
constexpr std::array<uint8_t, 4> aix_signature = {'A', 'I', 'X', 'F'};
constexpr std::array<uint8_t, 2> ac3_signature = {0x0B, 0x77};
constexpr uint8_t packet_stream_audio_min = 0xC0;
constexpr uint8_t packet_stream_audio_max = 0xDF;
constexpr uint8_t packet_stream_video_min = 0xE0;
constexpr uint8_t packet_stream_video_max = 0xEF;
constexpr uint8_t packet_system_header = 0xBB;
constexpr uint8_t packet_private_stream_1 = 0xBD;
constexpr uint8_t packet_padding_stream = 0xBE;
constexpr uint8_t packet_private_stream_2 = 0xBF;
constexpr size_t mpeg1_pack_header_size = 0x0C;
constexpr size_t sofdec_short_output_name_relative_offset = 32;
constexpr size_t sofdec_output_timestamp_relative_offset = 44;
constexpr size_t sofdec_builder_version_relative_offset = 64;
constexpr size_t sofdec_pack_size_relative_offset = 96;
constexpr size_t sofdec_min_header_packets_relative_offset = 104;
constexpr size_t sofdec_reserved_header_size_relative_offset = 108;
constexpr size_t sofdec_header_summary_relative_offset = 144;
constexpr size_t sofdec_bitrate_relative_offset = 148;
constexpr size_t sofdec_output_name_relative_offset = 192;
constexpr size_t sofdec_element_table_relative_offset = 352;
constexpr size_t sofdec_element_record_size = 64;
constexpr size_t sofdec_max_element_record_count = 27;
constexpr size_t sofdec_stream2_builder_version_relative_offset = 32;
constexpr size_t sofdec_stream2_output_name_relative_offset = 96;
constexpr size_t sofdec_stream2_header_summary_relative_offset = 160;
[[nodiscard]] bool is_audio_packet(uint8_t stream_id) noexcept {
    return stream_id >= packet_stream_audio_min && stream_id <= packet_stream_audio_max;
}

[[nodiscard]] bool is_video_packet(uint8_t stream_id) noexcept {
    return stream_id >= packet_stream_video_min && stream_id <= packet_stream_video_max;
}

[[nodiscard]] bool is_supported_packet(uint8_t stream_id) noexcept {
    return stream_id == packet_system_header ||
        stream_id == packet_private_stream_1 ||
        stream_id == packet_padding_stream ||
        stream_id == packet_private_stream_2 ||
        is_audio_packet(stream_id) ||
        is_video_packet(stream_id);
}

[[nodiscard]] size_t find_bytes(std::span<const uint8_t> data, std::span<const uint8_t> bytes) noexcept {
    const auto it = std::search(data.begin(), data.end(), bytes.begin(), bytes.end());
    if (it == data.end()) {
        return data.size();
    }
    return static_cast<size_t>(std::distance(data.begin(), it));
}

[[nodiscard]] bool has_legacy_sofdec_preamble(std::span<const uint8_t> data) noexcept {
    return std::ranges::starts_with(data, legacy_sofdec_stream_preamble);
}

[[nodiscard]] std::string read_c_string(std::span<const uint8_t> bytes) {
    size_t end = 0;
    while (end < bytes.size() && bytes[end] != 0) {
        ++end;
    }
    return trim_ascii(bytes.first(end));
}

[[nodiscard]] std::string read_c_string_at(
    std::span<const uint8_t> bytes,
    size_t offset,
    size_t capacity
) {
    return offset < bytes.size()
        ? read_c_string(bytes.subspan(offset, std::min(capacity, bytes.size() - offset)))
        : std::string{};
}

[[nodiscard]] SfdAudioType detect_audio_type(std::span<const uint8_t> payload) noexcept {
    if (std::ranges::starts_with(payload, aix_signature)) {
        return SfdAudioType::aix;
    }
    if (!payload.empty() && payload.front() == 0x80) {
        return SfdAudioType::adx;
    }
    if (std::ranges::starts_with(payload, ac3_signature)) {
        return SfdAudioType::ac3;
    }
    return SfdAudioType::unknown;
}

[[nodiscard]] SfdAudioType detect_private_stream_audio_type(std::span<const uint8_t> payload) noexcept {
    if (const auto direct = detect_audio_type(payload); direct != SfdAudioType::unknown) {
        return direct;
    }

    for (const size_t offset : {size_t{1}, size_t{4}}) {
        if (payload.size() >= offset + ac3_signature.size() &&
            std::equal(ac3_signature.begin(), ac3_signature.end(), payload.begin() + static_cast<std::ptrdiff_t>(offset))) {
            return SfdAudioType::ac3;
        }
    }

    return SfdAudioType::unknown;
}

struct VideoPayloadInfo {
    SfdVideoType type = SfdVideoType::unknown;
    std::optional<SfdVideoSequenceHeader> header;
};

[[nodiscard]] VideoPayloadInfo inspect_video_payload(std::span<const uint8_t> payload) {
    const auto sequence_header = video::parse_mpeg_sequence_header(payload);
    if (!sequence_header) {
        return {};
    }

    const auto video_type = video::detect_mpeg_video_type(payload);
    return VideoPayloadInfo{
        .type = video_type == video::MpegVideoType::mpeg2
            ? SfdVideoType::mpeg2
            : SfdVideoType::mpeg1,
        .header = SfdVideoSequenceHeader{
            .width = sequence_header->width,
            .height = sequence_header->height,
            .aspect_ratio_code = sequence_header->aspect_ratio_code,
            .frame_rate_code = sequence_header->frame_rate_code,
            .bit_rate_value = sequence_header->bit_rate_value,
        },
    };
}

[[nodiscard]] std::optional<size_t> parse_pes_payload_data_offset(std::span<const uint8_t> packet_payload) noexcept {
    size_t cursor = 0;
    while (cursor < packet_payload.size() && packet_payload[cursor] == 0xFF) {
        ++cursor;
    }

    if (cursor + 2 <= packet_payload.size() && (packet_payload[cursor] & 0xC0u) == 0x40u) {
        cursor += 2;
    }

    if (cursor >= packet_payload.size()) {
        return std::nullopt;
    }

    const uint8_t marker = packet_payload[cursor];
    if ((marker & 0xF0u) == 0x20u) {
        cursor += 5;
    } else if ((marker & 0xF0u) == 0x30u) {
        cursor += 10;
    } else if (marker == 0x0Fu) {
        cursor += 1;
    } else if ((marker & 0xC0u) == 0x80u) {
        if (cursor + 3 > packet_payload.size()) {
            return std::nullopt;
        }
        cursor += 3u + packet_payload[cursor + 2];
    }

    if (cursor > packet_payload.size()) {
        return std::nullopt;
    }
    return cursor;
}

template<typename T>
[[nodiscard]] std::string hex_value(T value) {
    std::ostringstream stream;
    stream << std::hex << std::uppercase << static_cast<uint64_t>(value);
    return stream.str();
}

[[nodiscard]] SfdElementRecord parse_sofdec_element_record(std::span<const uint8_t> record) {
    SfdElementRecord parsed;
    parsed.short_name = trim_ascii(record.first(12));
    parsed.timestamp = trim_ascii(record.subspan(12, 12));
    parsed.stream_id = record[24];
    parsed.source_type = record[25];
    std::copy_n(record.begin() + 26, parsed.detail_bytes.size(), parsed.detail_bytes.begin());
    std::copy_n(record.begin() + 32, parsed.footer_bytes.size(), parsed.footer_bytes.begin());

    if (is_video_packet(parsed.stream_id)) {
        parsed.picture_rate = read_le<uint16_t>(record.data() + 26);
        parsed.width = static_cast<uint16_t>((static_cast<uint16_t>(record[28]) << 4u) | (record[29] >> 4u));
        parsed.height = static_cast<uint16_t>((static_cast<uint16_t>(record[29] & 0x0Fu) << 8u) | record[30]);
        parsed.frame_rate_code = record[31];
    } else if (is_audio_packet(parsed.stream_id)) {
        parsed.audio_channels = record[27];
        parsed.audio_sample_rate = read_le<uint32_t>(record.data() + 28);
    }

    return parsed;
}

[[nodiscard]] std::optional<SfdHeaderSummary> parse_sofdec_header_summary(std::span<const uint8_t> payload) {
    const size_t stream2_label_offset = find_bytes(payload, sofdec_stream2_label);
    const size_t stream_label_offset = find_bytes(payload, sofdec_stream_label);

    size_t label_offset = payload.size();
    SfdHeaderVariant variant = SfdHeaderVariant::unknown;
    if (stream2_label_offset != payload.size()) {
        label_offset = stream2_label_offset;
        variant = SfdHeaderVariant::sofdec_stream2;
    } else if (stream_label_offset != payload.size()) {
        label_offset = stream_label_offset;
        variant = SfdHeaderVariant::sofdec_stream;
    }

    if (variant == SfdHeaderVariant::unknown) {
        return std::nullopt;
    }

    SfdHeaderSummary summary;
    summary.variant = variant;
    summary.header_label = variant == SfdHeaderVariant::sofdec_stream2
        ? std::string(sofdec_stream2_label.begin(), sofdec_stream2_label.end())
        : std::string(sofdec_stream_label.begin(), sofdec_stream_label.end());

    if (label_offset + sofdec_stream_label.size() > payload.size()) {
        return std::nullopt;
    }

    const size_t version_offset = label_offset + sofdec_stream_label.size();
    if (version_offset + 2 <= payload.size()) {
        summary.version_tag_bytes[0] = payload[version_offset + 0];
        summary.version_tag_bytes[1] = payload[version_offset + 1];
        summary.version_tag_size = 2;
    }
    if (variant == SfdHeaderVariant::sofdec_stream2 && version_offset + 4 <= payload.size()) {
        summary.version_tag_bytes[2] = payload[version_offset + 2];
        summary.version_tag_bytes[3] = payload[version_offset + 3];
        summary.version_tag_size = 4;
    }

    if (variant == SfdHeaderVariant::sofdec_stream2) {
        summary.builder_version = read_c_string_at(
            payload, label_offset + sofdec_stream2_builder_version_relative_offset, 64);
        summary.output_name = read_c_string_at(
            payload, label_offset + sofdec_stream2_output_name_relative_offset, 64);
        if (label_offset + sofdec_stream2_header_summary_relative_offset + 4 <= payload.size()) {
            summary.element_count = payload[label_offset + sofdec_stream2_header_summary_relative_offset + 0];
            summary.audio_count = payload[label_offset + sofdec_stream2_header_summary_relative_offset + 1];
            summary.video_count = payload[label_offset + sofdec_stream2_header_summary_relative_offset + 2];
            summary.private_count = payload[label_offset + sofdec_stream2_header_summary_relative_offset + 3];
        }
        return summary;
    }

    if (label_offset + sofdec_header_summary_relative_offset + 12 > payload.size()) {
        return std::nullopt;
    }

    summary.short_output_name = trim_ascii(payload.subspan(label_offset + sofdec_short_output_name_relative_offset, 12));
    summary.output_timestamp = trim_ascii(payload.subspan(label_offset + sofdec_output_timestamp_relative_offset, 12));
    summary.builder_version = trim_ascii(payload.subspan(label_offset + sofdec_builder_version_relative_offset, 32));
    summary.pack_size = read_le<uint32_t>(payload.data() + label_offset + sofdec_pack_size_relative_offset);
    summary.variable_pack = payload[label_offset + sofdec_pack_size_relative_offset + 4] != 0;
    summary.min_header_packet_count = read_le<uint16_t>(
        payload.data() + label_offset + sofdec_min_header_packets_relative_offset);
    summary.reserved_header_size = read_le<uint32_t>(
        payload.data() + label_offset + sofdec_reserved_header_size_relative_offset);
    summary.element_count = payload[label_offset + sofdec_header_summary_relative_offset + 0];
    summary.audio_count = payload[label_offset + sofdec_header_summary_relative_offset + 1];
    summary.video_count = payload[label_offset + sofdec_header_summary_relative_offset + 2];
    summary.private_count = payload[label_offset + sofdec_header_summary_relative_offset + 3];
    summary.bitrate_bytes_per_second = read_le<uint64_t>(
        payload.data() + label_offset + sofdec_bitrate_relative_offset);

    if ((summary.pack_size != 2048 && summary.pack_size != 4096) ||
        summary.element_count == 0 ||
        summary.element_count > sofdec_max_element_record_count ||
        static_cast<uint16_t>(summary.audio_count) +
            static_cast<uint16_t>(summary.video_count) +
            static_cast<uint16_t>(summary.private_count) != summary.element_count) {
        return std::nullopt;
    }

    summary.output_name = read_c_string_at(
        payload,
        label_offset + sofdec_output_name_relative_offset,
        sofdec_element_table_relative_offset - sofdec_output_name_relative_offset);

    const size_t record_count = std::min<size_t>(summary.element_count, sofdec_max_element_record_count);
    size_t record_offset = label_offset + sofdec_element_table_relative_offset;
    summary.element_records.reserve(record_count);
    for (size_t i = 0; i < record_count; ++i) {
        if (record_offset + sofdec_element_record_size > payload.size()) {
            break;
        }
        summary.element_records.push_back(parse_sofdec_element_record(
            payload.subspan(record_offset, sofdec_element_record_size)
        ));
        record_offset += sofdec_element_record_size;
    }

    return summary;
}

} // namespace

std::expected<void, std::string> SfdContainer::parse() {
    m_streams.clear();
    m_header_summary.reset();

    const auto data = m_source.bytes;
    if (data.size() < pack_start_code.size()) {
        return std::unexpected("SFD data is too small");
    }

    const size_t start_offset = find_bytes(data, pack_start_code);
    if (start_offset == data.size()) {
        return std::unexpected("SFD parse failed: could not find MPEG pack header");
    }

    std::array<int32_t, 256> stream_lookup{};
    stream_lookup.fill(-1);
    uint32_t next_audio_index = 0;
    uint32_t next_video_index = 0;
    uint32_t next_private_index = 0;

    size_t offset = start_offset;
    while (offset < data.size()) {
        if (offset + 4 > data.size()) {
            return std::unexpected("SFD packet header extends past the source size");
        }
        if (data[offset] != 0x00 || data[offset + 1] != 0x00 || data[offset + 2] != 0x01) {
            return std::unexpected("SFD parse failed: unexpected packet prefix at offset 0x" + hex_value(offset));
        }

        const uint8_t stream_id = data[offset + 3];
        if (stream_id == program_end_code[3]) {
            break;
        }

        if (stream_id == pack_start_code[3]) {
            if (offset + mpeg1_pack_header_size > data.size()) {
                return std::unexpected("SFD pack header extends past the source size");
            }
            offset += mpeg1_pack_header_size;
            continue;
        }

        if (!is_supported_packet(stream_id)) {
            return std::unexpected("SFD parse failed: unhandled packet stream id: 0x" + hex_value(stream_id));
        }

        if (offset + 6 > data.size()) {
            return std::unexpected("SFD packet size field extends past the source size");
        }

        const uint16_t packet_size = io::read_be<uint16_t>(data.data() + offset + 4);
        const size_t packet_payload_offset = offset + 6;
        const size_t packet_end = packet_payload_offset + packet_size;
        if (packet_end > data.size()) {
            return std::unexpected("SFD packet payload extends past the source size");
        }

        const auto packet_payload = data.subspan(packet_payload_offset, packet_size);
        if (stream_id == packet_private_stream_2) {
            if (!m_header_summary.has_value()) {
                m_header_summary = parse_sofdec_header_summary(packet_payload);
            }
        } else if (stream_id == packet_private_stream_1 || is_audio_packet(stream_id) || is_video_packet(stream_id)) {
            const auto data_offset_result = parse_pes_payload_data_offset(packet_payload);
            if (!data_offset_result) {
                return std::unexpected("SFD parse failed: could not parse PES payload header at offset 0x" + hex_value(offset));
            }

            const size_t data_offset = packet_payload_offset + *data_offset_result;
            if (data_offset < packet_end) {
                const uint32_t emitted_size = static_cast<uint32_t>(packet_end - data_offset);
                const auto payload = data.subspan(data_offset, emitted_size);

                const int32_t existing_stream_index = stream_lookup[stream_id];
                int32_t stream_index = existing_stream_index;
                if (stream_index < 0) {
                    SfdStream stream;
                    stream.index = static_cast<uint32_t>(m_streams.size());
                    stream.stream_id = stream_id;
                    if (is_audio_packet(stream_id)) {
                        stream.type = SfdStreamType::audio;
                    } else if (is_video_packet(stream_id)) {
                        stream.type = SfdStreamType::video;
                    } else if (stream.audio_type = detect_private_stream_audio_type(payload);
                               stream.audio_type != SfdAudioType::unknown) {
                        stream.type = SfdStreamType::audio;
                    } else {
                        stream.type = SfdStreamType::private_data;
                    }

                    if (stream.type == SfdStreamType::audio) {
                        stream.type_index = next_audio_index++;
                    } else if (stream.type == SfdStreamType::video) {
                        stream.type_index = next_video_index++;
                    } else {
                        stream.type_index = next_private_index++;
                    }
                    m_streams.push_back(std::move(stream));
                    stream_index = static_cast<int32_t>(m_streams.size() - 1);
                    stream_lookup[stream_id] = stream_index;
                }

                auto& stream = m_streams[static_cast<size_t>(stream_index)];
                if (stream.type == SfdStreamType::audio && stream.audio_type == SfdAudioType::unknown) {
                    stream.audio_type = stream_id == packet_private_stream_1
                        ? detect_private_stream_audio_type(payload)
                        : detect_audio_type(payload);
                }
                if (stream.type == SfdStreamType::video &&
                    (stream.video_type == SfdVideoType::unknown || !stream.video_header.has_value())) {
                    const auto video = inspect_video_payload(payload);
                    if (stream.video_type == SfdVideoType::unknown) {
                        stream.video_type = video.type;
                    }
                    if (!stream.video_header.has_value()) {
                        stream.video_header = video.header;
                    }
                }

                stream.packet_count += 1;
                stream.extracted_size += emitted_size;
                stream.chunks.push_back(SfdChunkSpan{
                    .source_offset = static_cast<uint64_t>(data_offset),
                    .size = emitted_size,
                });
            }
        }

        offset = packet_end;
    }

    if (m_streams.empty()) {
        return std::unexpected("SFD contained no extractable audio, video, or private streams");
    }
    if (!m_header_summary.has_value() && !has_legacy_sofdec_preamble(data)) {
        return std::unexpected("SFD parse failed: MPEG program stream has no SofDec header");
    }

    for (auto& stream : m_streams) {
        if (m_header_summary.has_value()) {
            const auto named = std::ranges::find_if(
                m_header_summary->element_records,
                [&stream](const SfdElementRecord& record) {
                    return record.stream_id == stream.stream_id && !record.short_name.empty();
                });
            if (named != m_header_summary->element_records.end()) {
                stream.source_name = named->short_name;
            }
            const auto it = std::find_if(
                m_header_summary->element_records.begin(),
                m_header_summary->element_records.end(),
                [&stream](const SfdElementRecord& record) {
                    return record.stream_id == stream.stream_id;
                }
            );
            if (it != m_header_summary->element_records.end()) {
                stream.element_record = *it;
                if (stream.source_name.empty()) {
                    stream.source_name = it->short_name;
                }
            }
        }
    }

    return {};
}

} // namespace cricodecs::sfd
