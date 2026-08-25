/**
 * @file aax_container.cpp
 * @brief AAX UTF-wrapper container implementation.
 *
 * Initial parsing behavior follows vgmstream's AAX loader shape. CriCodecs
 * build helpers and C++23 port work by Youjose.
 */

#include "aax_container.hpp"

#include "../utilities/io.hpp"

#include "../adx/adx_codec.hpp"

#include <algorithm>
#include <optional>

namespace cricodecs::aax {

namespace {

template <typename T>
std::expected<T, std::string> require_cell(
    std::expected<T, std::string> value,
    uint32_t row,
    std::string_view column
) {
    if (!value) {
        return std::unexpected(
            "Missing or invalid '" + std::string(column) + "' at row " +
            std::to_string(row) + ": " + value.error());
    }
    return *value;
}

std::expected<adx::AdxHeader, std::string> segment_header(std::span<const uint8_t> data, uint32_t index) {
    adx::AdxDecoder decoder;
    if (auto loaded = decoder.load(data); !loaded) {
        return std::unexpected(
            "AAX segment " + std::to_string(index) + " is not a valid ADX payload: " + loaded.error());
    }
    return decoder.header();
}

adx::AdxEncodeConfig encode_config(const adx::AdxHeader& header) {
    return {
        .sample_rate = header.sample_rate,
        .channels = header.channels,
        .bit_depth = header.bit_depth,
        .block_size = header.block_size,
        .encoding_mode = header.encoding_mode,
        .highpass_freq = header.highpass_freq,
        .version = header.version,
        .encryption_type = 0,
    };
}

adx::AdxLoop sample_loop(uint32_t start, uint32_t count) {
    return {
        .index = 0,
        .type = 1,
        .start_sample = start,
        .start_byte = 0,
        .end_sample = start + count,
        .end_byte = 0,
    };
}

} // namespace

std::expected<AaxContainer, std::string> AaxContainer::load(const std::filesystem::path& path) {
    auto source_bytes = io::read_file_bytes(path, "AAX load failed");
    if (!source_bytes) {
        return std::unexpected(source_bytes.error());
    }

    return load_owned(std::move(*source_bytes), path);
}

std::expected<AaxContainer, std::string> AaxContainer::load(std::span<const uint8_t> data) {
    return load_owned(std::vector<uint8_t>(data.begin(), data.end()));
}

std::expected<AaxContainer, std::string> AaxContainer::load_owned(
    std::vector<uint8_t> data,
    std::filesystem::path source_path
) {
    auto table = utf::UtfTable::load(std::move(data));
    if (!table) {
        return std::unexpected("AAX load failed: could not parse UTF table: " + table.error());
    }
    if (table->table_name() != "AAX") {
        return std::unexpected("AAX load failed: expected UTF table name AAX");
    }

    AaxContainer aax;
    aax.m_source_path = std::move(source_path);
    aax.m_table = std::move(*table);
    if (auto parsed = aax.parse(); !parsed) return std::unexpected(parsed.error());
    return aax;
}

std::expected<std::vector<AaxBuildEntry>, std::string> AaxContainer::build_entries() const {
    std::vector<AaxBuildEntry> entries;
    entries.reserve(m_segments.size());
    for (uint32_t index = 0; index < m_segments.size(); ++index) {
        auto data = raw_segment_data(index);
        if (!data) {
            return std::unexpected(data.error());
        }
        entries.push_back(AaxBuildEntry{
            .adx_data = std::vector<uint8_t>(data->begin(), data->end()),
            .loop_segment = m_segments[index].loop_segment,
        });
    }
    return entries;
}

std::expected<void, std::string> AaxContainer::replace_entries(std::vector<AaxBuildEntry> entries) {
    auto bytes = build(entries);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto replacement = load(*bytes);
    if (!replacement) {
        return std::unexpected("AAX edit failed: rebuilt container did not reload: " + replacement.error());
    }
    replacement->m_source_path = m_source_path;
    *this = std::move(*replacement);
    return {};
}

std::expected<void, std::string> AaxContainer::add_segment(
    std::span<const uint8_t> adx_data,
    bool loop_segment
) {
    auto entries = build_entries();
    if (!entries) {
        return std::unexpected(entries.error());
    }
    entries->push_back(AaxBuildEntry{
        .adx_data = std::vector<uint8_t>(adx_data.begin(), adx_data.end()),
        .loop_segment = loop_segment,
    });
    return replace_entries(std::move(*entries));
}

std::expected<void, std::string> AaxContainer::replace_segment(
    uint32_t index,
    std::span<const uint8_t> adx_data
) {
    if (index >= m_segments.size()) {
        return std::unexpected("AAX replace failed: segment index is out of range");
    }
    auto entries = build_entries();
    if (!entries) {
        return std::unexpected(entries.error());
    }
    (*entries)[index].adx_data.assign(adx_data.begin(), adx_data.end());
    return replace_entries(std::move(*entries));
}

std::expected<void, std::string> AaxContainer::remove_segment(uint32_t index) {
    if (index >= m_segments.size()) {
        return std::unexpected("AAX remove failed: segment index is out of range");
    }
    if (m_segments.size() == 1) {
        return std::unexpected("AAX remove failed: an AAX must retain at least one segment");
    }
    auto entries = build_entries();
    if (!entries) {
        return std::unexpected(entries.error());
    }
    entries->erase(entries->begin() + static_cast<std::ptrdiff_t>(index));
    return replace_entries(std::move(*entries));
}

std::expected<void, std::string> AaxContainer::move_segment(uint32_t from_index, uint32_t to_index) {
    if (from_index >= m_segments.size() || to_index >= m_segments.size()) {
        return std::unexpected("AAX move failed: segment index is out of range");
    }
    auto entries = build_entries();
    if (!entries) {
        return std::unexpected(entries.error());
    }
    if (from_index < to_index) {
        std::rotate(entries->begin() + from_index, entries->begin() + from_index + 1, entries->begin() + to_index + 1);
    } else if (from_index > to_index) {
        std::rotate(entries->begin() + to_index, entries->begin() + from_index, entries->begin() + from_index + 1);
    }
    return replace_entries(std::move(*entries));
}

std::expected<void, std::string> AaxContainer::set_loop_segment(uint32_t index, bool loop_segment) {
    if (index >= m_segments.size()) {
        return std::unexpected("AAX loop edit failed: segment index is out of range");
    }
    auto entries = build_entries();
    if (!entries) {
        return std::unexpected(entries.error());
    }
    (*entries)[index].loop_segment = loop_segment;
    return replace_entries(std::move(*entries));
}

std::expected<std::vector<uint8_t>, std::string> AaxContainer::build(std::span<const AaxBuildEntry> entries) {
    if (entries.empty()) {
        return std::unexpected("AAX build failed: no segments were provided");
    }

    utf::UtfTable table = utf::UtfTable::create("AAX");
    table.add_column("data", utf::ColumnType::VLData);
    table.add_column("lpflg", utf::ColumnType::UInt8);

    uint8_t expected_channels = 0;
    uint32_t expected_sample_rate = 0;

    for (uint32_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (entry.adx_data.empty()) {
            return std::unexpected("AAX segment " + std::to_string(i) + " is empty");
        }

        auto header = segment_header(entry.adx_data, i);
        if (!header) return std::unexpected(header.error());
        if (i == 0) {
            expected_channels = header->channels;
            expected_sample_rate = header->sample_rate;
        } else if (header->channels != expected_channels || header->sample_rate != expected_sample_rate) {
            return std::unexpected("AAX segments must share the same channel count and sample rate");
        }

        const uint32_t row = table.add_row();
        table.set(row, "data", entry.adx_data).value();
        table.set(row, "lpflg", static_cast<uint8_t>(entry.loop_segment ? 1 : 0)).value();
    }

    return table.build();
}

std::expected<void, std::string> AaxContainer::build_to_file(
    std::span<const AaxBuildEntry> entries,
    const std::filesystem::path& output_path
) {
    return build(entries).and_then([&](const auto& bytes) {
        return io::write_file_bytes(output_path, bytes, "AAX build failed");
    });
}

std::expected<void, std::string> AaxContainer::parse() {
    m_segments.clear();
    m_projected_segments.clear();
    m_channels = 0;
    m_sample_rate = 0;

    if (m_table.row_count() == 0) {
        return std::unexpected("AAX table has no rows");
    }

    bool found_non_empty_segment = false;

    for (uint32_t i = 0; i < m_table.row_count(); ++i) {
        auto loop_flag = require_cell(m_table.get<uint8_t>(i, "lpflg"), i, "lpflg");
        if (!loop_flag) {
            return std::unexpected(loop_flag.error());
        }

        auto data = require_cell(m_table.get_data(i, "data"), i, "data");
        if (!data) {
            return std::unexpected(data.error());
        }

        uint32_t segment_sample_count = 0;
        if (!data->empty()) {
            found_non_empty_segment = true;

            auto header = segment_header(*data, i);
            if (!header) return std::unexpected(header.error());
            if (m_channels == 0) {
                m_channels = header->channels;
                m_sample_rate = header->sample_rate;
            } else if (header->channels != m_channels || header->sample_rate != m_sample_rate) {
                return std::unexpected("AAX segments must share the same channel count and sample rate");
            }

            segment_sample_count = header->sample_count;
        }

        m_segments.push_back({
            .row_index = i,
            .data_size = static_cast<uint32_t>(data->size()),
            .sample_count = segment_sample_count,
            .loop_segment = (*loop_flag != 0),
        });
    }

    if (!found_non_empty_segment) {
        return std::unexpected("AAX contains no ADX segment data");
    }

    m_projected_segments.resize(m_segments.size());
    return {};
}

bool AaxContainer::has_loop_segments() const noexcept {
    return std::ranges::any_of(m_segments, &AaxSegmentInfo::loop_segment);
}

uint32_t AaxContainer::sample_count() const noexcept {
    uint32_t count = 0;
    for (const auto& segment : m_segments) count += segment.sample_count;
    return count;
}

std::expected<std::span<const uint8_t>, std::string> AaxContainer::raw_segment_data(uint32_t index) const {
    if (index >= m_segments.size()) {
        return std::unexpected("AAX segment index is out of range");
    }

    const uint32_t row = m_segments[index].row_index;
    auto data = m_table.get_data(row, "data");
    if (!data) {
        return std::unexpected(
            "Missing or invalid 'data' at row " + std::to_string(row) + ": " + data.error());
    }

    return *data;
}

std::expected<std::span<const uint8_t>, std::string> AaxContainer::segment_data(uint32_t index) const {
    if (index >= m_segments.size()) {
        return std::unexpected("AAX segment index is out of range");
    }

    auto raw_data = raw_segment_data(index);
    if (!raw_data) {
        return std::unexpected(raw_data.error());
    }

    const auto& segment = m_segments[index];
    if (!segment.loop_segment || segment.sample_count == 0) {
        return *raw_data;
    }

    adx::AdxDecoder decoder;
    auto load_result = decoder.load(*raw_data);
    if (!load_result) {
        return std::unexpected(
            "AAX segment " + std::to_string(index) + " loop projection failed: " + load_result.error());
    }
    if (decoder.has_loops()) {
        return *raw_data;
    }
    if (decoder.is_ahx()) {
        return std::unexpected(
            "AAX segment " + std::to_string(index) + " loop projection failed: AHX loop metadata is not supported");
    }
    if (decoder.is_encrypted()) {
        return std::unexpected(
            "AAX segment " + std::to_string(index) + " loop projection failed: encrypted ADX requires a key");
    }

    auto& projected = m_projected_segments[index];
    if (projected.empty()) {
        auto decoded = decoder.decode();
        if (!decoded) {
            return std::unexpected(
                "AAX segment " + std::to_string(index) + " loop projection failed: " + decoded.error());
        }

        const auto config = encode_config(decoder.header());
        const auto loop = sample_loop(0, decoded->sample_count);

        auto encoded = adx::AdxEncoder::encode(decoded->pcm_data, config, std::span<const adx::AdxLoop>(&loop, 1));
        if (!encoded) {
            return std::unexpected(
                "AAX segment " + std::to_string(index) + " loop projection failed: " + encoded.error());
        }
        projected = std::move(*encoded);
    }

    return std::span<const uint8_t>(projected);
}

std::expected<void, std::string> AaxContainer::extract_file(
    uint32_t index,
    const std::filesystem::path& output_path
) const {
    auto data = segment_data(index);
    if (!data) {
        return std::unexpected(data.error());
    }

    return io::write_file_bytes(output_path, *data, "AAX extract failed");
}

std::expected<std::vector<uint8_t>, std::string> AaxContainer::adx_data() const {
    if (!has_loop_segments()) {
        size_t total_size = 0;
        for (const auto& segment : m_segments) total_size += segment.data_size;

        std::vector<uint8_t> output;
        output.reserve(total_size);
        for (uint32_t i = 0; i < segment_count(); ++i) {
            auto segment = raw_segment_data(i);
            if (!segment) {
                return std::unexpected(segment.error());
            }
            output.insert(output.end(), segment->begin(), segment->end());
        }
        return output;
    }

    std::vector<int16_t> pcm;
    std::vector<adx::AdxLoop> loops;
    std::optional<adx::AdxEncodeConfig> config;
    uint32_t accumulated_samples = 0;
    pcm.reserve(static_cast<size_t>(sample_count()) * m_channels);

    for (uint32_t i = 0; i < segment_count(); ++i) {
        auto segment = raw_segment_data(i);
        if (!segment) {
            return std::unexpected(segment.error());
        }

        adx::AdxDecoder decoder;
        auto load_result = decoder.load(*segment);
        if (!load_result) {
            return std::unexpected(
                "AAX ADX export failed: segment " + std::to_string(i) + " is not valid ADX: " + load_result.error());
        }
        if (decoder.is_ahx()) {
            return std::unexpected("AAX ADX export failed: AHX segment export is not supported");
        }
        if (decoder.is_encrypted()) {
            return std::unexpected("AAX ADX export failed: encrypted ADX segment requires a key");
        }

        auto decoded = decoder.decode();
        if (!decoded) {
            return std::unexpected(
                "AAX ADX export failed: could not decode segment " + std::to_string(i) + ": " + decoded.error());
        }

        if (!config) config = encode_config(decoder.header());

        if (m_segments[i].loop_segment && decoded->sample_count != 0) {
            loops.assign(1, sample_loop(accumulated_samples, decoded->sample_count));
        }

        pcm.insert(pcm.end(), decoded->pcm_data.begin(), decoded->pcm_data.end());
        accumulated_samples += decoded->sample_count;
    }

    if (pcm.empty()) {
        return std::unexpected("AAX ADX export failed: no decoded PCM was produced");
    }

    auto encoded = adx::AdxEncoder::encode(pcm, *config, loops);
    if (!encoded) {
        return std::unexpected("AAX ADX export failed: " + encoded.error());
    }
    return *encoded;
}

std::expected<std::vector<uint8_t>, std::string> AaxContainer::save() const {
    return build_entries().and_then([](const auto& entries) { return build(entries); });
}

std::expected<void, std::string> AaxContainer::save_to_file(const std::filesystem::path& output_path) const {
    return save().and_then([&](const auto& bytes) {
        return io::write_file_bytes(output_path, bytes, "AAX save failed");
    });
}

std::expected<void, std::string> AaxContainer::export_adx(const std::filesystem::path& output_path) const {
    return adx_data().and_then([&](const auto& bytes) {
        return io::write_file_bytes(output_path, bytes, "AAX export failed");
    });
}

} // namespace cricodecs::aax
