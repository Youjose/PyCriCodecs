/**
 * @file aix_reader.cpp
 * @brief AIX segmented/layered ADX container reader
 *
 * The layout handling is initially based on the vgmstream AIX implementation.
 * Parsing is checked against official `aixmux` output.
 * C++23 reader implementation by Youjose.
 */

#include "aix_container.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace cricodecs::aix {

namespace {

using io::read_be;

constexpr io::FourCC aixf_magic{"AIXF"};
constexpr uint32_t aixp_magic = io::FourCC{"AIXP"}.be_value();
constexpr uint32_t aixe_magic = io::FourCC{"AIXE"}.be_value();
constexpr uint32_t supported_version = 0x01000014u;
constexpr size_t fixed_header_size = 0x20;
constexpr size_t segment_entry_size = 0x10;
constexpr size_t layer_list_header_size = 0x08;
constexpr size_t layer_entry_size = 0x08;
constexpr size_t aixp_header_size = 0x10;
constexpr size_t expected_header_size = 0x800;
constexpr uint32_t padded_segment_shift = 0x800;
constexpr uint16_t adx_signature = 0x8000;
constexpr size_t max_inferred_loop_segments = 5;

bool has_block_magic(std::span<const uint8_t> view, uint32_t offset, uint32_t magic) {
    return
        offset <= view.size() &&
        sizeof(uint32_t) <= view.size() - offset &&
        read_be<uint32_t>(view.data() + offset) == magic;
}

bool normalize_segment_offsets_for_padding(std::vector<AixSegment>& segments, std::span<const uint8_t> view) {
    for (size_t index = 0; index < segments.size(); ++index) {
        const uint32_t offset = segments[index].offset;
        if (has_block_magic(view, offset, aixp_magic) || has_block_magic(view, offset, aixe_magic)) {
            continue;
        }

        const uint64_t padded_offset = static_cast<uint64_t>(offset) + padded_segment_shift;
        if (padded_offset > std::numeric_limits<uint32_t>::max()) {
            continue;
        }
        if (!has_block_magic(view, static_cast<uint32_t>(padded_offset), aixp_magic)) {
            continue;
        }

        bool in_bounds = true;
        for (size_t remaining = index; remaining < segments.size(); ++remaining) {
            const uint64_t shifted = static_cast<uint64_t>(segments[remaining].offset) + padded_segment_shift;
            if (shifted > view.size()) {
                in_bounds = false;
                break;
            }
        }
        if (!in_bounds) {
            continue;
        }

        for (size_t remaining = index; remaining < segments.size(); ++remaining) {
            segments[remaining].offset += padded_segment_shift;
        }

        for (size_t remaining = index; remaining + 1 < segments.size(); ++remaining) {
            segments[remaining].size = segments[remaining + 1].offset - segments[remaining].offset;
        }

        const uint64_t tail_size = view.size() - segments.back().offset;
        if (tail_size > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
        segments.back().size = static_cast<uint32_t>(tail_size);
        return true;
    }

    return true;
}

bool uses_padded_first_payload(const uint8_t* block, uint32_t block_size) {
    if (block_size < 0x28 || block_size < aixp_header_size) {
        return false;
    }

    return
        read_be<uint32_t>(block + 0x10) == 0 &&
        read_be<uint16_t>(block + block_size - 0x28) == adx_signature;
}

uint64_t sum_segment_samples(std::span<const AixSegment> segments, size_t end_exclusive) {
    uint64_t total_samples = 0;
    for (size_t index = 0; index < end_exclusive; ++index) {
        total_samples += static_cast<uint64_t>(std::max(segments[index].sample_count, 0));
    }
    return total_samples;
}

uint64_t total_segment_samples(std::span<const AixSegment> segments) {
    return sum_segment_samples(segments, segments.size());
}

std::optional<AixLoopInfo> infer_container_loop(std::span<const AixSegment> segments, bool force_disable_loop) {
    if (force_disable_loop || segments.size() <= 1 || segments.size() > max_inferred_loop_segments) {
        return std::nullopt;
    }

    const size_t loop_start_segment = segments.size() > 3 ? 2 : 1;
    const size_t loop_end_segment = segments.size() > 3 ? segments.size() - 2 : 1;
    if (loop_start_segment >= segments.size() || loop_end_segment >= segments.size() || loop_start_segment > loop_end_segment) {
        return std::nullopt;
    }

    return AixLoopInfo{
        .start_segment = loop_start_segment,
        .end_segment = loop_end_segment,
        .start_sample = sum_segment_samples(segments, loop_start_segment),
        .end_sample = sum_segment_samples(segments, loop_end_segment + 1),
    };
}

} // namespace

std::expected<void, AixError> Aix::load(const std::filesystem::path& path) {
    m_owned_bytes.clear();
    m_source_path = path;
    if (auto result = m_reader.open(path); !result) {
        return std::unexpected("AIX load failed: could not open input: " + std::string(result.error()));
    }
    return parse();
}

std::expected<void, AixError> Aix::load(std::span<const uint8_t> data) {
    m_owned_bytes.assign(data.begin(), data.end());
    m_source_path.clear();
    if (auto result = m_reader.open(std::span<const uint8_t>(m_owned_bytes.data(), m_owned_bytes.size())); !result) {
        return std::unexpected("AIX load failed: could not open memory buffer: " + std::string(result.error()));
    }
    return parse();
}

std::expected<void, AixError> Aix::load(std::vector<uint8_t>&& data) {
    m_owned_bytes = std::move(data);
    m_source_path.clear();
    if (auto result = m_reader.open(std::span<const uint8_t>(m_owned_bytes.data(), m_owned_bytes.size())); !result) {
        return std::unexpected("AIX load failed: could not open memory buffer: " + std::string(result.error()));
    }
    return parse();
}

std::expected<void, AixError> Aix::parse() {
    m_segments.clear();
    m_layers.clear();
    m_segment_packets.clear();
    m_total_sample_count = 0;
    m_inferred_loop.reset();

    const auto view = m_reader.data();
    if (view.size() < fixed_header_size) {
        return std::unexpected("AIX data is too small");
    }
    if (!std::equal(aixf_magic.begin(), aixf_magic.end(), view.begin())) {
        return std::unexpected("AIX parse failed: invalid magic");
    }

    const uint32_t version = read_be<uint32_t>(view.data() + 0x08);
    if (version != supported_version) {
        return std::unexpected("AIX parse failed: unsupported version");
    }
    if (read_be<uint32_t>(view.data() + 0x0C) != expected_header_size) {
        return std::unexpected("AIX parse failed: unsupported header size");
    }

    const uint32_t data_offset = read_be<uint32_t>(view.data() + 0x04) + 0x08u;
    if (data_offset > view.size()) {
        return std::unexpected("AIX data offset is out of bounds");
    }

    const uint16_t segment_count = read_be<uint16_t>(view.data() + 0x18);
    if (segment_count == 0 || segment_count > max_segments) {
        return std::unexpected("AIX segment count is invalid");
    }

    const uint64_t segment_table_end = fixed_header_size + static_cast<uint64_t>(segment_count) * segment_entry_size;
    if (segment_table_end > data_offset) {
        return std::unexpected("AIX segment table overlaps the stream data");
    }

    std::vector<AixSegment> parsed_segments;
    parsed_segments.reserve(segment_count);
    for (uint16_t index = 0; index < segment_count; ++index) {
        const size_t entry_offset = fixed_header_size + static_cast<size_t>(index) * segment_entry_size;

        AixSegment segment;
        segment.offset = read_be<uint32_t>(view.data() + entry_offset + 0x00);
        segment.size = read_be<uint32_t>(view.data() + entry_offset + 0x04);
        segment.sample_count = read_be<int32_t>(view.data() + entry_offset + 0x08);
        segment.sample_rate = read_be<int32_t>(view.data() + entry_offset + 0x0C);

        if (index > 0 && segment.sample_rate == 0 && !parsed_segments.empty()) {
            segment.sample_rate = parsed_segments.front().sample_rate;
        }
        if (!parsed_segments.empty() && segment.sample_rate != parsed_segments.front().sample_rate) {
            return std::unexpected("AIX segments use mismatched sample rates");
        }

        parsed_segments.push_back(segment);
    }

    if (!normalize_segment_offsets_for_padding(parsed_segments, view)) {
        return std::unexpected("AIX repaired segment sizes exceeded the supported range");
    }

    uint32_t expected_first_segment_offset = data_offset;
    if (expected_first_segment_offset != parsed_segments.front().offset) {
        const uint64_t padded_data_offset = static_cast<uint64_t>(data_offset) + padded_segment_shift;
        if (padded_data_offset == parsed_segments.front().offset) {
            expected_first_segment_offset = static_cast<uint32_t>(padded_data_offset);
        }
    }

    if (parsed_segments.front().offset != expected_first_segment_offset) {
        return std::unexpected("AIX first segment offset did not match the stream data offset");
    }

    bool force_disable_inferred_loop = false;
    if (
        parsed_segments.size() == 3 &&
        parsed_segments[1].offset < view.size() &&
        parsed_segments[1].size > view.size() - parsed_segments[1].offset
    ) {
        parsed_segments.resize(2);
        parsed_segments[1].size = static_cast<uint32_t>(view.size() - parsed_segments[1].offset);
        force_disable_inferred_loop = true;
    }

    for (const auto& segment : parsed_segments) {
        if (segment.offset > view.size() || segment.size > view.size() - segment.offset) {
            return std::unexpected("AIX segment data is out of bounds");
        }
    }
    const size_t subtable_offset = fixed_header_size + static_cast<size_t>(segment_count) * segment_entry_size;
    if (subtable_offset + 0x10 > data_offset) {
        return std::unexpected("AIX layer metadata is truncated");
    }
    if (view[subtable_offset] != 0x01) {
        return std::unexpected("AIX layer metadata marker is invalid");
    }

    const size_t layer_list_offset = subtable_offset + 0x10;
    if (layer_list_offset + layer_list_header_size > data_offset) {
        return std::unexpected("AIX layer table header is truncated");
    }

    const uint8_t layer_count = view[layer_list_offset];
    if (layer_count == 0) {
        return std::unexpected("AIX layer count is invalid");
    }

    const uint64_t layer_table_end =
        layer_list_offset + layer_list_header_size + static_cast<uint64_t>(layer_count) * layer_entry_size;
    if (layer_table_end > data_offset) {
        return std::unexpected("AIX layer table overlaps the stream data");
    }

    std::vector<AixLayer> parsed_layers;
    parsed_layers.reserve(layer_count);
    for (uint8_t index = 0; index < layer_count; ++index) {
        const size_t entry_offset =
            layer_list_offset + layer_list_header_size + static_cast<size_t>(index) * layer_entry_size;

        AixLayer layer;
        layer.sample_rate = read_be<uint32_t>(view.data() + entry_offset + 0x00);
        layer.channel_count = io::read_le<uint32_t>(view.data() + entry_offset + 0x04);

        if (layer.sample_rate != static_cast<uint32_t>(parsed_segments.front().sample_rate)) {
            return std::unexpected("AIX layer sample rate did not match the segment sample rate");
        }
        if (layer.channel_count == 0) {
            return std::unexpected("AIX layer channel count is invalid");
        }

        parsed_layers.push_back(layer);
    }

    std::vector<std::vector<AixPacket>> parsed_segment_packets;
    parsed_segment_packets.reserve(parsed_segments.size());
    for (const auto& segment : parsed_segments) {
        std::vector<AixPacket> packets;
        size_t block_offset = segment.offset;
        const size_t segment_end = static_cast<size_t>(segment.offset) + segment.size;

        while (block_offset + 8 <= segment_end) {
            const uint32_t block_id = read_be<uint32_t>(view.data() + block_offset + 0x00);

            // vgmstream treats AIXE as a data-less terminal marker. Producers
            // disagree on whether its size field describes the padded tail or
            // the compact eight-byte marker, so do not apply AIXP sizing to it.
            if (block_id == aixe_magic) {
                break;
            }

            const uint64_t block_size =
                static_cast<uint64_t>(read_be<uint32_t>(view.data() + block_offset + 0x04)) + 0x08u;
            if (block_size > segment_end - block_offset) {
                return std::unexpected("AIX block extends past the segment");
            }

            if (block_id == aixp_magic) {
                if (block_size < aixp_header_size) {
                    return std::unexpected("AIXP block is too small");
                }

                AixPacket packet;
                packet.file_offset = static_cast<uint32_t>(block_offset);
                packet.total_size = static_cast<uint32_t>(block_size);
                packet.layer_index = static_cast<int8_t>(view[block_offset + 0x08]);
                packet.payload_size = read_be<uint16_t>(view.data() + block_offset + 0x0A);

                if (view[block_offset + 0x09] != parsed_layers.size()) {
                    return std::unexpected("AIXP block layer count did not match the AIX header");
                }
                if (packet.layer_index < 0 || static_cast<size_t>(packet.layer_index) >= parsed_layers.size()) {
                    return std::unexpected("AIXP block layer index is out of range");
                }
                if (aixp_header_size + packet.payload_size > block_size) {
                    return std::unexpected("AIXP block payload is truncated");
                }

                packets.push_back(packet);
            }

            block_offset += static_cast<size_t>(block_size);
        }

        if (packets.empty()) {
            return std::unexpected("AIX segment did not contain any AIXP blocks");
        }

        parsed_segment_packets.push_back(std::move(packets));
    }

    m_segments = std::move(parsed_segments);
    m_layers = std::move(parsed_layers);
    m_segment_packets = std::move(parsed_segment_packets);
    m_total_sample_count = total_segment_samples(m_segments);
    m_inferred_loop = infer_container_loop(m_segments, force_disable_inferred_loop);
    return {};
}

std::expected<Aix::LayerPayloads, AixError> Aix::layer_payloads(
    size_t segment_index,
    size_t layer_index
) const {
    if (segment_index >= m_segments.size()) {
        return std::unexpected("AIX segment index is out of range");
    }
    if (layer_index >= m_layers.size()) {
        return std::unexpected("AIX layer index is out of range");
    }

    const auto view = m_reader.data();
    if (segment_index >= m_segment_packets.size()) {
        return std::unexpected("AIX segment packet table is not loaded");
    }
    const auto& packets = m_segment_packets[segment_index];

    LayerPayloads payloads;
    payloads.spans.reserve(packets.size() / std::max<size_t>(m_layers.size(), 1));
    bool first_payload = true;
    for (const auto& packet : packets) {
        if (packet.layer_index != static_cast<int8_t>(layer_index)) {
            continue;
        }

        const auto* block = view.data() + packet.file_offset;
        size_t payload_offset = aixp_header_size;
        size_t payload_size = packet.payload_size;

        if (first_payload && uses_padded_first_payload(block, packet.total_size)) {
            payload_offset = packet.total_size - 0x28;
            payload_size = 0x28;
        }

        if (payload_size > std::numeric_limits<size_t>::max() - payloads.total_size) {
            return std::unexpected("AIX segment layer payload is too large");
        }

        payloads.spans.emplace_back(block + payload_offset, payload_size);
        payloads.total_size += payload_size;
        first_payload = false;
    }

    if (payloads.total_size == 0) {
        return std::unexpected("AIX segment did not contain the requested layer");
    }
    return payloads;
}

std::expected<std::vector<uint8_t>, AixError> Aix::segment_bytes(
    size_t segment_index,
    size_t layer_index
) const {
    auto payloads = layer_payloads(segment_index, layer_index);
    if (!payloads) {
        return std::unexpected(payloads.error());
    }

    std::vector<uint8_t> output;
    output.reserve(payloads->total_size);
    for (const auto payload : payloads->spans) {
        output.insert(output.end(), payload.begin(), payload.end());
    }

    return output;
}

std::expected<void, AixError> Aix::extract_file(
    size_t segment_index,
    size_t layer_index,
    const std::filesystem::path& output_path
) const {
    auto payloads = layer_payloads(segment_index, layer_index);
    if (!payloads) {
        return std::unexpected(payloads.error());
    }

    if (const auto parent = output_path.parent_path(); !parent.empty()) {
        std::error_code filesystem_error;
        std::filesystem::create_directories(parent, filesystem_error);
        if (filesystem_error) {
            return std::unexpected("AIX extract failed: could not create output directory: " + filesystem_error.message());
        }
    }

    io::writer writer;
    if (auto open_result = writer.open(output_path); !open_result) {
        return std::unexpected("AIX extract failed: could not open layer output: " + output_path.string() + ": " + open_result.error());
    }
    for (const auto payload : payloads->spans) {
        if (auto write_result = writer.write(payload); !write_result) {
            return std::unexpected("AIX extract failed: could not write layer output: " + output_path.string() + ": " + write_result.error());
        }
    }
    if (auto close_result = writer.close(); !close_result) {
        return std::unexpected("AIX extract failed: could not finalize layer output: " + output_path.string() + ": " + close_result.error());
    }

    return {};
}

std::expected<void, AixError> Aix::extract_all(const std::filesystem::path& output_dir) const {
    std::error_code filesystem_error;
    std::filesystem::create_directories(output_dir, filesystem_error);
    if (filesystem_error) {
        return std::unexpected("AIX extract failed: could not create output directory: " + filesystem_error.message());
    }

    for (size_t segment_index = 0; segment_index < m_segments.size(); ++segment_index) {
        for (size_t layer_index = 0; layer_index < m_layers.size(); ++layer_index) {
            const auto output_path = output_dir /
                ("segment_" + std::to_string(segment_index) + "_layer_" + std::to_string(layer_index) + ".adx");
            if (auto extract_result = extract_file(segment_index, layer_index, output_path); !extract_result) {
                return std::unexpected(extract_result.error());
            }
        }
    }

    return {};
}

} // namespace cricodecs::aix
