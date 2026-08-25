/**
 * @file aix_builder.cpp
 * @brief AIX layered ADX mux builder.
 *
 * Builder behavior is based on the official `aixmux` list-file behavior.
 * The C++23 mux is CriCodecs work by Youjose.
 */

#include "aix_container.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>
#include <span>
#include <string_view>

#include "../adx/adx_codec.hpp"
#include "../utilities/numeric.hpp"

namespace cricodecs::aix {

namespace {

using io::write_be;
using io::write_le;
using util::align_up;

constexpr io::FourCC aixf_magic{"AIXF"};
constexpr uint32_t aixp_magic = io::FourCC{"AIXP"}.be_value();
constexpr uint32_t aixe_magic = io::FourCC{"AIXE"}.be_value();
constexpr uint32_t supported_version = 0x01000014u;
constexpr uint32_t expected_header_size = 0x800u;
constexpr uint32_t first_segment_offset = 0x1800u;
constexpr uint32_t segment_alignment = 0x800u;
constexpr size_t fixed_header_size = 0x20;
constexpr size_t segment_entry_size = 0x10;
constexpr size_t layer_list_header_size = 0x08;
constexpr size_t layer_entry_size = 0x08;
constexpr size_t aixp_header_size = 0x10;
constexpr size_t aixe_header_size = 0x08;
constexpr size_t official_build_limit = 32;
constexpr uint32_t first_sequence = 0xFFFFFFFFu;
constexpr uint32_t final_sequence = 0xFFFFFFFEu;
constexpr uint16_t adx_signature = 0x8000;

struct PreparedLayer {
    std::span<const uint8_t> bytes;
    adx::AdxHeader header{};
    uint16_t first_payload_size = 0;
    uint16_t tail_payload_size = 0;
    uint32_t steady_payload_size = 0;
    size_t middle_bytes = 0;
};

[[nodiscard]] std::string build_error(std::string_view detail) {
    return "AIX build failed: " + std::string(detail);
}

[[nodiscard]] std::string indexed_build_error(
    size_t segment_index,
    std::optional<size_t> layer_index,
    std::string_view detail
) {
    std::string error = "AIX build failed: segment " + std::to_string(segment_index);
    if (layer_index.has_value()) {
        error += " layer " + std::to_string(*layer_index);
    }
    error += ": ";
    error += detail;
    return error;
}

[[nodiscard]] bool headers_match_for_same_layer(
    const adx::AdxHeader& lhs,
    const adx::AdxHeader& rhs
) noexcept {
    return
        lhs.signature == rhs.signature &&
        lhs.data_offset == rhs.data_offset &&
        lhs.encoding_mode == rhs.encoding_mode &&
        lhs.block_size == rhs.block_size &&
        lhs.bit_depth == rhs.bit_depth &&
        lhs.channels == rhs.channels &&
        lhs.sample_rate == rhs.sample_rate &&
        lhs.highpass_freq == rhs.highpass_freq &&
        lhs.version == rhs.version &&
        lhs.flags == rhs.flags;
}

void append_aix_packet(
    std::vector<uint8_t>& segment_bytes,
    uint8_t layer_index,
    uint8_t layer_count,
    uint32_t sequence,
    std::span<const uint8_t> payload
) {
    const size_t packet_offset = segment_bytes.size();
    segment_bytes.resize(packet_offset + aixp_header_size + payload.size(), 0);

    auto packet = std::span<uint8_t>(segment_bytes).subspan(packet_offset, aixp_header_size + payload.size());
    std::copy(aixf_magic.begin(), aixf_magic.end(), packet.begin());
    write_be<uint32_t>(packet.data() + 0x00, aixp_magic);
    write_be<uint32_t>(packet.data() + 0x04, static_cast<uint32_t>(aixp_header_size + payload.size() - aixe_header_size));
    packet[0x08] = layer_index;
    packet[0x09] = layer_count;
    write_be<uint16_t>(packet.data() + 0x0A, static_cast<uint16_t>(payload.size()));
    write_be<uint32_t>(packet.data() + 0x0C, sequence);
    std::copy(payload.begin(), payload.end(), packet.begin() + static_cast<std::ptrdiff_t>(aixp_header_size));
}

void append_aixe_block(std::vector<uint8_t>& segment_bytes) {
    const size_t aligned_size = static_cast<size_t>(align_up(segment_bytes.size() + aixe_header_size, segment_alignment));
    const size_t aixe_total_size = aligned_size - segment_bytes.size();
    const size_t block_offset = segment_bytes.size();

    segment_bytes.resize(aligned_size, 0);
    write_be<uint32_t>(segment_bytes.data() + block_offset + 0x00, aixe_magic);
    write_be<uint32_t>(
        segment_bytes.data() + block_offset + 0x04,
        static_cast<uint32_t>(aixe_total_size - aixe_header_size));
}

[[nodiscard]] std::expected<std::vector<PreparedLayer>, AixError> prepare_segment(
    const AixBuildSegment& segment,
    size_t segment_index
) {
    if (segment.layer_adx_data.empty()) {
        return std::unexpected(indexed_build_error(segment_index, std::nullopt, "must contain at least one layer"));
    }
    if (segment.layer_adx_data.size() > official_build_limit) {
        return std::unexpected(indexed_build_error(segment_index, std::nullopt, "exceeds the official 32-layer builder limit"));
    }

    std::vector<PreparedLayer> prepared;
    prepared.reserve(segment.layer_adx_data.size());

    for (size_t layer_index = 0; layer_index < segment.layer_adx_data.size(); ++layer_index) {
        const auto& layer_bytes = segment.layer_adx_data[layer_index];
        if (layer_bytes.empty()) {
            return std::unexpected(indexed_build_error(segment_index, layer_index, "ADX data is empty"));
        }

        auto adx_file = adx::Adx::load(std::span<const uint8_t>(layer_bytes.data(), layer_bytes.size()));
        if (!adx_file) {
            return std::unexpected(indexed_build_error(segment_index, layer_index, adx_file.error()));
        }

        const auto& header = adx_file->header();
        if (header.signature != adx_signature) {
            return std::unexpected(indexed_build_error(segment_index, layer_index, "does not start with an ADX stream"));
        }
        if (adx_file->is_ahx()) {
            return std::unexpected(indexed_build_error(segment_index, layer_index, "AHX-in-ADX streams are not supported"));
        }
        if (header.sample_rate == 0 || header.sample_count == 0) {
            return std::unexpected(indexed_build_error(segment_index, layer_index, "uses an invalid ADX sample rate or sample count"));
        }
        if (header.block_size == 0) {
            return std::unexpected(indexed_build_error(segment_index, layer_index, "uses an invalid ADX block size"));
        }

        const uint32_t first_payload_size = static_cast<uint32_t>(header.data_offset) + 4u;
        if (first_payload_size > std::numeric_limits<uint16_t>::max()) {
            return std::unexpected(indexed_build_error(segment_index, layer_index, "uses an ADX header larger than the AIX packet field supports"));
        }
        if (layer_bytes.size() < first_payload_size + header.block_size) {
            return std::unexpected(indexed_build_error(segment_index, layer_index, "is too small for the reviewed AIX packetization model"));
        }

        const size_t middle_bytes = layer_bytes.size() - first_payload_size - header.block_size;
        const uint32_t steady_payload_size = static_cast<uint32_t>(header.block_size) * 150u;
        if (steady_payload_size > std::numeric_limits<uint16_t>::max()) {
            return std::unexpected(indexed_build_error(segment_index, layer_index, "uses an ADX block size too large for the reviewed steady-state packet size"));
        }

        PreparedLayer layer{
            .bytes = std::span<const uint8_t>(layer_bytes.data(), layer_bytes.size()),
            .header = header,
            .first_payload_size = static_cast<uint16_t>(first_payload_size),
            .tail_payload_size = header.block_size,
            .steady_payload_size = steady_payload_size,
            .middle_bytes = middle_bytes,
        };

        if (!prepared.empty()) {
            const auto& reference = prepared.front();
            if (header.sample_rate != reference.header.sample_rate) {
                return std::unexpected(indexed_build_error(segment_index, layer_index, "sample rate does not match the other layers in the segment"));
            }
            if (header.sample_count != reference.header.sample_count) {
                return std::unexpected(indexed_build_error(segment_index, layer_index, "sample count does not match the other layers in the segment"));
            }
            if (layer.first_payload_size != reference.first_payload_size ||
                layer.tail_payload_size != reference.tail_payload_size ||
                layer.steady_payload_size != reference.steady_payload_size ||
                layer.middle_bytes != reference.middle_bytes) {
                return std::unexpected(indexed_build_error(
                    segment_index,
                    layer_index,
                    "does not match the reviewed per-layer packet cadence of the other layers in the segment"));
            }
        }

        prepared.push_back(layer);
    }

    return prepared;
}

[[nodiscard]] std::expected<std::vector<uint8_t>, AixError> build_segment_bytes(
    std::span<const PreparedLayer> layers,
    size_t segment_index
) {
    std::vector<uint8_t> segment_bytes;
    const auto layer_count = static_cast<uint8_t>(layers.size());

    for (size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
        const auto& layer = layers[layer_index];
        append_aix_packet(
            segment_bytes,
            static_cast<uint8_t>(layer_index),
            layer_count,
            first_sequence,
            layer.bytes.first(layer.first_payload_size)
        );
    }

    const size_t middle_total = layers.front().middle_bytes;
    std::vector<size_t> middle_offsets(layers.size(), layers.front().first_payload_size);
    size_t remaining_middle = middle_total;
    uint32_t sequence = 0;

    while (remaining_middle > 0) {
        const size_t chunk_size = std::min(
            remaining_middle,
            static_cast<size_t>(layers.front().steady_payload_size));
        for (size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
            const auto& layer = layers[layer_index];
            append_aix_packet(
                segment_bytes,
                static_cast<uint8_t>(layer_index),
                layer_count,
                sequence,
                layer.bytes.subspan(middle_offsets[layer_index], chunk_size)
            );
            middle_offsets[layer_index] += chunk_size;
        }
        remaining_middle -= chunk_size;
        if (sequence == std::numeric_limits<uint32_t>::max() - 1u && remaining_middle > 0) {
            return std::unexpected(indexed_build_error(segment_index, std::nullopt, "would overflow the reviewed AIX sequence field"));
        }
        ++sequence;
    }

    for (size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
        const auto& layer = layers[layer_index];
        append_aix_packet(
            segment_bytes,
            static_cast<uint8_t>(layer_index),
            layer_count,
            final_sequence,
            layer.bytes.last(layer.tail_payload_size)
        );
    }

    append_aixe_block(segment_bytes);
    if (segment_bytes.size() > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected(indexed_build_error(segment_index, std::nullopt, "segment grew past the supported size range"));
    }
    return segment_bytes;
}

void move_item(auto& items, size_t from, size_t to) {
    if (from < to) {
        std::rotate(items.begin() + static_cast<std::ptrdiff_t>(from),
            items.begin() + static_cast<std::ptrdiff_t>(from + 1),
            items.begin() + static_cast<std::ptrdiff_t>(to + 1));
    } else if (from > to) {
        std::rotate(items.begin() + static_cast<std::ptrdiff_t>(to),
            items.begin() + static_cast<std::ptrdiff_t>(from),
            items.begin() + static_cast<std::ptrdiff_t>(from + 1));
    }
}

} // namespace

std::expected<std::vector<uint8_t>, AixError> Aix::build(std::span<const AixBuildSegment> segments) {
    if (segments.empty()) {
        return std::unexpected(build_error("at least one segment is required"));
    }
    if (segments.size() > official_build_limit) {
        return std::unexpected(build_error("the reviewed builder currently supports at most 32 segments"));
    }

    std::vector<std::vector<PreparedLayer>> prepared_segments;
    prepared_segments.reserve(segments.size());
    for (size_t segment_index = 0; segment_index < segments.size(); ++segment_index) {
        auto prepared = prepare_segment(segments[segment_index], segment_index);
        if (!prepared) {
            return std::unexpected(prepared.error());
        }
        prepared_segments.push_back(std::move(*prepared));
    }

    const size_t layer_count = prepared_segments.front().size();
    const auto& reference_layers = prepared_segments.front();

    for (size_t segment_index = 1; segment_index < prepared_segments.size(); ++segment_index) {
        const auto& segment = prepared_segments[segment_index];
        if (segment.size() != layer_count) {
            return std::unexpected(indexed_build_error(
                segment_index,
                std::nullopt,
                "layer count does not match the first segment"));
        }
        if (segment.front().header.sample_rate != reference_layers.front().header.sample_rate) {
            return std::unexpected(indexed_build_error(
                segment_index,
                std::nullopt,
                "sample rate does not match the first segment"));
        }

        for (size_t layer_index = 0; layer_index < layer_count; ++layer_index) {
            if (!headers_match_for_same_layer(reference_layers[layer_index].header, segment[layer_index].header)) {
                return std::unexpected(indexed_build_error(
                    segment_index,
                    layer_index,
                    "does not match the reviewed ADX framing of the same layer in the first segment"));
            }
        }
    }

    std::vector<std::vector<uint8_t>> segment_blobs;
    segment_blobs.reserve(prepared_segments.size());
    for (size_t segment_index = 0; segment_index < prepared_segments.size(); ++segment_index) {
        auto blob = build_segment_bytes(prepared_segments[segment_index], segment_index);
        if (!blob) {
            return std::unexpected(blob.error());
        }
        segment_blobs.push_back(std::move(*blob));
    }

    std::vector<uint8_t> output(first_segment_offset, 0);
    std::copy(aixf_magic.begin(), aixf_magic.end(), output.begin());
    write_be<uint32_t>(output.data() + 0x04, first_segment_offset - 8u);
    write_be<uint32_t>(output.data() + 0x08, supported_version);
    write_be<uint32_t>(output.data() + 0x0C, expected_header_size);
    write_be<uint16_t>(output.data() + 0x18, static_cast<uint16_t>(prepared_segments.size()));

    const auto& first_layer = prepared_segments.front().front();
    if (first_layer.first_payload_size >= 6u) {
        const auto cri_trailer = first_layer.bytes.subspan(first_layer.first_payload_size - 6u, 6u);
        std::copy(cri_trailer.begin(), cri_trailer.end(), output.begin() + static_cast<std::ptrdiff_t>(first_segment_offset - 6u));
    }

    uint32_t segment_offset = first_segment_offset;
    for (size_t segment_index = 0; segment_index < prepared_segments.size(); ++segment_index) {
        const size_t entry_offset = fixed_header_size + segment_index * segment_entry_size;
        const auto segment_size = segment_blobs[segment_index].size();
        if (segment_size > std::numeric_limits<uint32_t>::max()) {
            return std::unexpected(indexed_build_error(segment_index, std::nullopt, "segment exceeded the supported size range"));
        }

        write_be<uint32_t>(output.data() + entry_offset + 0x00, segment_offset);
        write_be<uint32_t>(output.data() + entry_offset + 0x04, static_cast<uint32_t>(segment_size));
        const auto& header = prepared_segments[segment_index].front().header;
        write_be<uint32_t>(output.data() + entry_offset + 0x08, header.sample_count);
        write_be<uint32_t>(output.data() + entry_offset + 0x0C, header.sample_rate);

        if (segment_size > std::numeric_limits<uint32_t>::max() - segment_offset) {
            return std::unexpected(indexed_build_error(segment_index, std::nullopt, "segment offsets exceeded the supported archive size range"));
        }
        segment_offset += static_cast<uint32_t>(segment_size);
    }

    const size_t subtable_offset = fixed_header_size + prepared_segments.size() * segment_entry_size;
    output[subtable_offset] = 0x01;
    write_be<uint32_t>(output.data() + subtable_offset + 0x08, 1u);
    const uint32_t total_channel_count = std::accumulate(
        reference_layers.begin(),
        reference_layers.end(),
        0u,
        [](uint32_t total, const PreparedLayer& layer) {
            return total + layer.header.channels;
        });
    const uint32_t reviewed_mode_flag =
        (prepared_segments.size() > 1 || total_channel_count == 8u) ? 1u : 0u;
    write_be<uint32_t>(output.data() + subtable_offset + 0x0C, reviewed_mode_flag);

    const size_t layer_list_offset = subtable_offset + 0x10;
    output[layer_list_offset] = static_cast<uint8_t>(layer_count);
    for (size_t layer_index = 0; layer_index < layer_count; ++layer_index) {
        const size_t entry_offset = layer_list_offset + layer_list_header_size + layer_index * layer_entry_size;
        write_be<uint32_t>(output.data() + entry_offset + 0x00, reference_layers[layer_index].header.sample_rate);
        write_le<uint32_t>(output.data() + entry_offset + 0x04, reference_layers[layer_index].header.channels);
    }

    for (const auto& segment_blob : segment_blobs) {
        output.insert(output.end(), segment_blob.begin(), segment_blob.end());
    }

    return output;
}

std::expected<void, AixError> Aix::build_to_file(
    std::span<const AixBuildSegment> segments,
    const std::filesystem::path& output_path
) {
    auto bytes = build(segments);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    if (const auto parent = output_path.parent_path(); !parent.empty()) {
        std::error_code filesystem_error;
        std::filesystem::create_directories(parent, filesystem_error);
        if (filesystem_error) {
            return std::unexpected("AIX build failed: failed to create output directory: " + filesystem_error.message());
        }
    }

    io::writer writer;
    if (auto open_result = writer.open(output_path); !open_result) {
        return std::unexpected("AIX build failed: failed to open output file: " + output_path.string() + ": " + open_result.error());
    }
    if (auto write_result = writer.write(std::span<const uint8_t>(bytes->data(), bytes->size())); !write_result) {
        return std::unexpected("AIX build failed: failed to write output file: " + output_path.string() + ": " + write_result.error());
    }
    if (auto close_result = writer.close(); !close_result) {
        return std::unexpected("AIX build failed: failed to finalize output file: " + output_path.string() + ": " + close_result.error());
    }

    return {};
}

std::expected<std::vector<AixBuildSegment>, AixError> Aix::build_segments() const {
    std::vector<AixBuildSegment> result;
    result.reserve(m_segments.size());
    for (size_t segment_index = 0; segment_index < m_segments.size(); ++segment_index) {
        AixBuildSegment segment;
        segment.layer_adx_data.reserve(m_layers.size());
        for (size_t layer_index = 0; layer_index < m_layers.size(); ++layer_index) {
            auto bytes = segment_bytes(segment_index, layer_index);
            if (!bytes) {
                return std::unexpected(bytes.error());
            }
            segment.layer_adx_data.push_back(std::move(*bytes));
        }
        result.push_back(std::move(segment));
    }
    return result;
}

std::expected<void, AixError> Aix::replace_segments(std::vector<AixBuildSegment> segments) {
    auto bytes = build(segments);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    const auto original_path = m_source_path;
    Aix replacement;
    if (auto loaded = replacement.load(std::move(*bytes)); !loaded) {
        return std::unexpected("AIX edit failed: rebuilt container did not reload: " + loaded.error());
    }
    replacement.m_source_path = original_path;
    *this = std::move(replacement);
    return {};
}

std::expected<std::vector<uint8_t>, AixError> Aix::save() const {
    auto segments = build_segments();
    if (!segments) {
        return std::unexpected(segments.error());
    }
    return build(*segments);
}

std::expected<void, AixError> Aix::save_to_file(const std::filesystem::path& output_path) const {
    auto segments = build_segments();
    if (!segments) {
        return std::unexpected(segments.error());
    }
    return build_to_file(*segments, output_path);
}

std::expected<void, AixError> Aix::add_segment(AixBuildSegment segment) {
    auto segments = build_segments();
    if (!segments) {
        return std::unexpected(segments.error());
    }
    segments->push_back(std::move(segment));
    return replace_segments(std::move(*segments));
}

std::expected<void, AixError> Aix::replace_segment(size_t segment_index, AixBuildSegment segment) {
    auto segments = build_segments();
    if (!segments) {
        return std::unexpected(segments.error());
    }
    if (segment_index >= segments->size()) {
        return std::unexpected("AIX replace failed: segment index is out of range");
    }
    (*segments)[segment_index] = std::move(segment);
    return replace_segments(std::move(*segments));
}

std::expected<void, AixError> Aix::remove_segment(size_t segment_index) {
    auto segments = build_segments();
    if (!segments) {
        return std::unexpected(segments.error());
    }
    if (segment_index >= segments->size()) {
        return std::unexpected("AIX remove failed: segment index is out of range");
    }
    if (segments->size() == 1) {
        return std::unexpected("AIX remove failed: an AIX must retain at least one segment");
    }
    segments->erase(segments->begin() + static_cast<std::ptrdiff_t>(segment_index));
    return replace_segments(std::move(*segments));
}

std::expected<void, AixError> Aix::move_segment(size_t from_index, size_t to_index) {
    auto segments = build_segments();
    if (!segments) {
        return std::unexpected(segments.error());
    }
    if (from_index >= segments->size() || to_index >= segments->size()) {
        return std::unexpected("AIX move failed: segment index is out of range");
    }
    move_item(*segments, from_index, to_index);
    return replace_segments(std::move(*segments));
}

std::expected<void, AixError> Aix::add_layer(std::vector<std::vector<uint8_t>> segment_adx_data) {
    auto segments = build_segments();
    if (!segments) {
        return std::unexpected(segments.error());
    }
    if (segment_adx_data.size() != segments->size()) {
        return std::unexpected("AIX add layer failed: provide one ADX payload for every segment");
    }
    for (size_t segment_index = 0; segment_index < segments->size(); ++segment_index) {
        (*segments)[segment_index].layer_adx_data.push_back(std::move(segment_adx_data[segment_index]));
    }
    return replace_segments(std::move(*segments));
}

std::expected<void, AixError> Aix::replace_layer(
    size_t segment_index,
    size_t layer_index,
    std::span<const uint8_t> adx_data
) {
    auto segments = build_segments();
    if (!segments) {
        return std::unexpected(segments.error());
    }
    if (segment_index >= segments->size() ||
        layer_index >= (*segments)[segment_index].layer_adx_data.size()) {
        return std::unexpected("AIX replace failed: segment or layer index is out of range");
    }
    (*segments)[segment_index].layer_adx_data[layer_index].assign(adx_data.begin(), adx_data.end());
    return replace_segments(std::move(*segments));
}

std::expected<void, AixError> Aix::remove_layer(size_t layer_index) {
    auto segments = build_segments();
    if (!segments) {
        return std::unexpected(segments.error());
    }
    if (layer_index >= m_layers.size()) {
        return std::unexpected("AIX remove failed: layer index is out of range");
    }
    if (m_layers.size() == 1) {
        return std::unexpected("AIX remove failed: an AIX must retain at least one layer");
    }
    for (auto& segment : *segments) {
        segment.layer_adx_data.erase(
            segment.layer_adx_data.begin() + static_cast<std::ptrdiff_t>(layer_index));
    }
    return replace_segments(std::move(*segments));
}

std::expected<void, AixError> Aix::move_layer(size_t from_index, size_t to_index) {
    auto segments = build_segments();
    if (!segments) {
        return std::unexpected(segments.error());
    }
    if (from_index >= m_layers.size() || to_index >= m_layers.size()) {
        return std::unexpected("AIX move failed: layer index is out of range");
    }
    for (auto& segment : *segments) {
        move_item(segment.layer_adx_data, from_index, to_index);
    }
    return replace_segments(std::move(*segments));
}

} // namespace cricodecs::aix
