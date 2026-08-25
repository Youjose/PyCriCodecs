/**
 * @file aix_builder.cpp
 * @brief AIX layered ADX mux builder.
 *
 * Builder behavior is based on the official `aixmux` list-file behavior.
 * The C++23 mux is CriCodecs work by Youjose.
 */

#include "aix_container.hpp"

#include <algorithm>
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
};

[[nodiscard]] constexpr size_t first_payload_size(const PreparedLayer& layer) noexcept {
    return static_cast<size_t>(layer.header.data_offset) + 4;
}

[[nodiscard]] constexpr size_t middle_payload_size(const PreparedLayer& layer) noexcept {
    return layer.bytes.size() - first_payload_size(layer) - layer.header.block_size;
}

[[nodiscard]] std::string build_error(std::string_view detail) {
    return "AIX build failed: " + std::string(detail);
}

[[nodiscard]] std::string segment_error(size_t segment, std::string_view detail) {
    return "AIX build failed: segment " + std::to_string(segment) + ": " + std::string(detail);
}

[[nodiscard]] std::string layer_error(size_t segment, size_t layer, std::string_view detail) {
    return "AIX build failed: segment " + std::to_string(segment) +
        " layer " + std::to_string(layer) + ": " + std::string(detail);
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
    std::vector<uint8_t>& output,
    uint8_t layer_index,
    uint8_t layer_count,
    uint32_t sequence,
    std::span<const uint8_t> payload
) {
    const size_t packet_offset = output.size();
    output.resize(packet_offset + aixp_header_size + payload.size(), 0);

    auto packet = std::span<uint8_t>(output).subspan(packet_offset, aixp_header_size + payload.size());
    write_be<uint32_t>(packet.data() + 0x00, aixp_magic);
    write_be<uint32_t>(packet.data() + 0x04, static_cast<uint32_t>(aixp_header_size + payload.size() - aixe_header_size));
    packet[0x08] = layer_index;
    packet[0x09] = layer_count;
    write_be<uint16_t>(packet.data() + 0x0A, static_cast<uint16_t>(payload.size()));
    write_be<uint32_t>(packet.data() + 0x0C, sequence);
    std::copy(payload.begin(), payload.end(), packet.begin() + static_cast<std::ptrdiff_t>(aixp_header_size));
}

void append_aixe_block(std::vector<uint8_t>& output) {
    const size_t aligned_size = static_cast<size_t>(align_up(output.size() + aixe_header_size, segment_alignment));
    const size_t aixe_total_size = aligned_size - output.size();
    const size_t block_offset = output.size();

    output.resize(aligned_size, 0);
    write_be<uint32_t>(output.data() + block_offset + 0x00, aixe_magic);
    write_be<uint32_t>(
        output.data() + block_offset + 0x04,
        static_cast<uint32_t>(aixe_total_size - aixe_header_size));
}

[[nodiscard]] std::expected<std::vector<PreparedLayer>, AixError> prepare_segment(
    const AixBuildSegment& segment,
    size_t segment_index
) {
    if (segment.layer_adx_data.empty()) {
        return std::unexpected(segment_error(segment_index, "must contain at least one layer"));
    }
    if (segment.layer_adx_data.size() > official_build_limit) {
        return std::unexpected(segment_error(segment_index, "exceeds the official 32-layer builder limit"));
    }

    std::vector<PreparedLayer> prepared;
    prepared.reserve(segment.layer_adx_data.size());

    for (size_t layer_index = 0; layer_index < segment.layer_adx_data.size(); ++layer_index) {
        const auto& layer_bytes = segment.layer_adx_data[layer_index];
        if (layer_bytes.empty()) {
            return std::unexpected(layer_error(segment_index, layer_index, "ADX data is empty"));
        }

        auto adx_file = adx::Adx::load(std::span<const uint8_t>(layer_bytes.data(), layer_bytes.size()));
        if (!adx_file) {
            return std::unexpected(layer_error(segment_index, layer_index, adx_file.error()));
        }

        const auto& header = adx_file->header();
        if (header.signature != adx_signature) {
            return std::unexpected(layer_error(segment_index, layer_index, "does not start with an ADX stream"));
        }
        if (adx_file->is_ahx()) {
            return std::unexpected(layer_error(segment_index, layer_index, "AHX-in-ADX streams are not supported"));
        }
        if (header.sample_rate == 0 || header.sample_count == 0) {
            return std::unexpected(layer_error(segment_index, layer_index, "uses an invalid ADX sample rate or sample count"));
        }
        if (header.block_size == 0) {
            return std::unexpected(layer_error(segment_index, layer_index, "uses an invalid ADX block size"));
        }

        const uint32_t first_payload_size = static_cast<uint32_t>(header.data_offset) + 4u;
        if (first_payload_size > std::numeric_limits<uint16_t>::max()) {
            return std::unexpected(layer_error(segment_index, layer_index, "uses an ADX header larger than the AIX packet field supports"));
        }
        if (layer_bytes.size() < first_payload_size + header.block_size) {
            return std::unexpected(layer_error(segment_index, layer_index, "is too small for the reviewed AIX packetization model"));
        }

        const uint32_t steady_payload_size = static_cast<uint32_t>(header.block_size) * 150u;
        if (steady_payload_size > std::numeric_limits<uint16_t>::max()) {
            return std::unexpected(layer_error(segment_index, layer_index, "uses an ADX block size too large for the reviewed steady-state packet size"));
        }

        PreparedLayer layer{
            .bytes = std::span<const uint8_t>(layer_bytes.data(), layer_bytes.size()),
            .header = header,
        };

        if (!prepared.empty()) {
            const auto& reference = prepared.front();
            if (header.sample_rate != reference.header.sample_rate) {
                return std::unexpected(layer_error(segment_index, layer_index, "sample rate does not match the other layers in the segment"));
            }
            if (header.sample_count != reference.header.sample_count) {
                return std::unexpected(layer_error(segment_index, layer_index, "sample count does not match the other layers in the segment"));
            }
            if (header.data_offset != reference.header.data_offset ||
                header.block_size != reference.header.block_size ||
                layer.bytes.size() != reference.bytes.size()) {
                return std::unexpected(layer_error(
                    segment_index,
                    layer_index,
                    "does not match the reviewed per-layer packet cadence of the other layers in the segment"));
            }
        }

        prepared.push_back(layer);
    }

    return prepared;
}

[[nodiscard]] std::expected<uint32_t, AixError> append_segment(
    std::vector<uint8_t>& output,
    std::span<const PreparedLayer> layers,
    size_t segment_index
) {
    const size_t segment_offset = output.size();
    const auto layer_count = static_cast<uint8_t>(layers.size());
    const size_t first_size = first_payload_size(layers.front());
    const size_t middle_size = middle_payload_size(layers.front());
    const size_t steady_size = static_cast<size_t>(layers.front().header.block_size) * 150;
    const size_t tail_size = layers.front().header.block_size;

    for (size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
        append_aix_packet(
            output,
            static_cast<uint8_t>(layer_index),
            layer_count,
            first_sequence,
            layers[layer_index].bytes.first(first_size)
        );
    }

    size_t middle_offset = first_size;
    uint32_t sequence = 0;

    while (middle_offset < first_size + middle_size) {
        const size_t chunk_size = std::min(steady_size, first_size + middle_size - middle_offset);
        for (size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
            append_aix_packet(
                output,
                static_cast<uint8_t>(layer_index),
                layer_count,
                sequence,
                layers[layer_index].bytes.subspan(middle_offset, chunk_size)
            );
        }
        middle_offset += chunk_size;
        if (sequence == std::numeric_limits<uint32_t>::max() - 1u && middle_offset < first_size + middle_size) {
            return std::unexpected(segment_error(segment_index, "would overflow the reviewed AIX sequence field"));
        }
        ++sequence;
    }

    for (size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
        append_aix_packet(
            output,
            static_cast<uint8_t>(layer_index),
            layer_count,
            final_sequence,
            layers[layer_index].bytes.last(tail_size)
        );
    }

    // Each segment starts aligned, so aligning the archive pads this AIXE tail.
    append_aixe_block(output);
    if (output.size() > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected(segment_error(segment_index, "segment offsets exceeded the supported archive size range"));
    }
    return static_cast<uint32_t>(output.size() - segment_offset);
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
            return std::unexpected(segment_error(
                segment_index,
                "layer count does not match the first segment"));
        }
        if (segment.front().header.sample_rate != reference_layers.front().header.sample_rate) {
            return std::unexpected(segment_error(
                segment_index,
                "sample rate does not match the first segment"));
        }

        for (size_t layer_index = 0; layer_index < layer_count; ++layer_index) {
            if (!headers_match_for_same_layer(reference_layers[layer_index].header, segment[layer_index].header)) {
                return std::unexpected(layer_error(
                    segment_index,
                    layer_index,
                    "does not match the reviewed ADX framing of the same layer in the first segment"));
            }
        }
    }

    std::vector<uint8_t> output(first_segment_offset, 0);
    std::copy(aixf_magic.begin(), aixf_magic.end(), output.begin());
    write_be<uint32_t>(output.data() + 0x04, first_segment_offset - 8u);
    write_be<uint32_t>(output.data() + 0x08, supported_version);
    write_be<uint32_t>(output.data() + 0x0C, expected_header_size);
    write_be<uint16_t>(output.data() + 0x18, static_cast<uint16_t>(prepared_segments.size()));

    const auto& first_layer = prepared_segments.front().front();
    const size_t first_size = first_payload_size(first_layer);
    if (first_size >= 6) {
        const auto cri_trailer = first_layer.bytes.subspan(first_size - 6, 6);
        std::copy(cri_trailer.begin(), cri_trailer.end(), output.begin() + static_cast<std::ptrdiff_t>(first_segment_offset - 6u));
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

    for (size_t segment_index = 0; segment_index < prepared_segments.size(); ++segment_index) {
        const uint32_t segment_offset = static_cast<uint32_t>(output.size());
        auto segment_size = append_segment(output, prepared_segments[segment_index], segment_index);
        if (!segment_size) {
            return std::unexpected(segment_size.error());
        }
        const size_t entry_offset = fixed_header_size + segment_index * segment_entry_size;
        const auto& header = prepared_segments[segment_index].front().header;
        write_be<uint32_t>(output.data() + entry_offset + 0x00, segment_offset);
        write_be<uint32_t>(output.data() + entry_offset + 0x04, *segment_size);
        write_be<uint32_t>(output.data() + entry_offset + 0x08, header.sample_count);
        write_be<uint32_t>(output.data() + entry_offset + 0x0C, header.sample_rate);
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

    return io::write_file_bytes(output_path, *bytes, "AIX build failed");
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
    return edit_segments([&](auto& segments) -> std::expected<void, AixError> {
        segments.push_back(std::move(segment));
        return {};
    });
}

std::expected<void, AixError> Aix::replace_segment(size_t segment_index, AixBuildSegment segment) {
    return edit_segments([&](auto& segments) -> std::expected<void, AixError> {
        if (segment_index >= segments.size()) {
            return std::unexpected("AIX replace failed: segment index is out of range");
        }
        segments[segment_index] = std::move(segment);
        return {};
    });
}

std::expected<void, AixError> Aix::remove_segment(size_t segment_index) {
    return edit_segments([&](auto& segments) -> std::expected<void, AixError> {
        if (segment_index >= segments.size()) {
            return std::unexpected("AIX remove failed: segment index is out of range");
        }
        if (segments.size() == 1) {
            return std::unexpected("AIX remove failed: an AIX must retain at least one segment");
        }
        segments.erase(segments.begin() + static_cast<std::ptrdiff_t>(segment_index));
        return {};
    });
}

std::expected<void, AixError> Aix::move_segment(size_t from_index, size_t to_index) {
    return edit_segments([&](auto& segments) -> std::expected<void, AixError> {
        if (std::max(from_index, to_index) >= segments.size()) {
            return std::unexpected("AIX move failed: segment index is out of range");
        }
        move_item(segments, from_index, to_index);
        return {};
    });
}

std::expected<void, AixError> Aix::add_layer(std::vector<std::vector<uint8_t>> segment_adx_data) {
    return edit_segments([&](auto& segments) -> std::expected<void, AixError> {
        if (segment_adx_data.size() != segments.size()) {
            return std::unexpected("AIX add layer failed: provide one ADX payload for every segment");
        }
        for (size_t index = 0; index < segments.size(); ++index) {
            segments[index].layer_adx_data.push_back(std::move(segment_adx_data[index]));
        }
        return {};
    });
}

std::expected<void, AixError> Aix::replace_layer(
    size_t segment_index,
    size_t layer_index,
    std::span<const uint8_t> adx_data
) {
    return edit_segments([&](auto& segments) -> std::expected<void, AixError> {
        if (segment_index >= segments.size() ||
            layer_index >= segments[segment_index].layer_adx_data.size()) {
            return std::unexpected("AIX replace failed: segment or layer index is out of range");
        }
        segments[segment_index].layer_adx_data[layer_index].assign(adx_data.begin(), adx_data.end());
        return {};
    });
}

std::expected<void, AixError> Aix::remove_layer(size_t layer_index) {
    return edit_segments([&](auto& segments) -> std::expected<void, AixError> {
        if (layer_index >= m_layers.size()) {
            return std::unexpected("AIX remove failed: layer index is out of range");
        }
        if (m_layers.size() == 1) {
            return std::unexpected("AIX remove failed: an AIX must retain at least one layer");
        }
        for (auto& segment : segments) {
            segment.layer_adx_data.erase(
                segment.layer_adx_data.begin() + static_cast<std::ptrdiff_t>(layer_index));
        }
        return {};
    });
}

std::expected<void, AixError> Aix::move_layer(size_t from_index, size_t to_index) {
    return edit_segments([&](auto& segments) -> std::expected<void, AixError> {
        if (std::max(from_index, to_index) >= m_layers.size()) {
            return std::unexpected("AIX move failed: layer index is out of range");
        }
        for (auto& segment : segments) {
            move_item(segment.layer_adx_data, from_index, to_index);
        }
        return {};
    });
}

} // namespace cricodecs::aix
