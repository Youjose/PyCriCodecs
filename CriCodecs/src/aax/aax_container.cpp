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

namespace cricodecs::aax {

namespace {

template <typename T>
std::expected<T, std::string> require_value(const utf::UtfTable& table, uint32_t row, std::string_view column) {
    auto value = table.get<T>(row, column);
    if (!value) {
        return std::unexpected(
            "Missing or invalid '" + std::string(column) + "' at row " +
            std::to_string(row) + ": " + value.error());
    }
    return *value;
}

std::expected<std::span<const uint8_t>, std::string> require_data(
    const utf::UtfTable& table,
    uint32_t row,
    std::string_view column
) {
    auto value = table.get_data(row, column);
    if (!value) {
        return std::unexpected(
            "Missing or invalid '" + std::string(column) + "' at row " +
            std::to_string(row) + ": " + value.error());
    }
    return *value;
}

} // namespace

std::expected<AaxContainer, std::string> AaxContainer::load(const std::filesystem::path& path) {
    auto source_bytes = io::read_file_bytes(path, "AAX load failed");
    if (!source_bytes) {
        return std::unexpected(source_bytes.error());
    }

    AaxContainer aax;
    aax.m_owned_source = std::move(*source_bytes);
    aax.m_source = std::span<const uint8_t>(aax.m_owned_source);
    aax.m_source_path = path;

    auto table = utf::UtfTable::load(aax.m_source);
    if (!table) {
        return std::unexpected("AAX load failed: could not parse UTF table: " + table.error());
    }
    if (table->table_name() != "AAX") {
        return std::unexpected("AAX load failed: expected UTF table name AAX");
    }

    aax.m_table = std::move(*table);

    auto parse_result = aax.parse();
    if (!parse_result) {
        return std::unexpected(parse_result.error());
    }

    return aax;
}

std::expected<AaxContainer, std::string> AaxContainer::load(std::span<const uint8_t> data) {
    AaxContainer aax;
    aax.m_owned_source.assign(data.begin(), data.end());
    aax.m_source_path.clear();
    aax.m_source = std::span<const uint8_t>(aax.m_owned_source);

    auto table = utf::UtfTable::load(aax.m_source);
    if (!table) {
        return std::unexpected("AAX load failed: could not parse UTF table: " + table.error());
    }
    if (table->table_name() != "AAX") {
        return std::unexpected("AAX load failed: expected UTF table name AAX");
    }

    aax.m_table = std::move(*table);

    auto parse_result = aax.parse();
    if (!parse_result) {
        return std::unexpected(parse_result.error());
    }

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
    auto entries = build_entries();
    if (!entries) {
        return std::unexpected(entries.error());
    }
    if (index >= entries->size()) {
        return std::unexpected("AAX replace failed: segment index is out of range");
    }
    (*entries)[index].adx_data.assign(adx_data.begin(), adx_data.end());
    return replace_entries(std::move(*entries));
}

std::expected<void, std::string> AaxContainer::remove_segment(uint32_t index) {
    auto entries = build_entries();
    if (!entries) {
        return std::unexpected(entries.error());
    }
    if (index >= entries->size()) {
        return std::unexpected("AAX remove failed: segment index is out of range");
    }
    if (entries->size() == 1) {
        return std::unexpected("AAX remove failed: an AAX must retain at least one segment");
    }
    entries->erase(entries->begin() + static_cast<std::ptrdiff_t>(index));
    return replace_entries(std::move(*entries));
}

std::expected<void, std::string> AaxContainer::move_segment(uint32_t from_index, uint32_t to_index) {
    auto entries = build_entries();
    if (!entries) {
        return std::unexpected(entries.error());
    }
    if (from_index >= entries->size() || to_index >= entries->size()) {
        return std::unexpected("AAX move failed: segment index is out of range");
    }
    if (from_index < to_index) {
        std::rotate(entries->begin() + from_index, entries->begin() + from_index + 1, entries->begin() + to_index + 1);
    } else if (from_index > to_index) {
        std::rotate(entries->begin() + to_index, entries->begin() + from_index, entries->begin() + from_index + 1);
    }
    return replace_entries(std::move(*entries));
}

std::expected<void, std::string> AaxContainer::set_loop_segment(uint32_t index, bool loop_segment) {
    auto entries = build_entries();
    if (!entries) {
        return std::unexpected(entries.error());
    }
    if (index >= entries->size()) {
        return std::unexpected("AAX loop edit failed: segment index is out of range");
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

        adx::AdxDecoder decoder;
        auto load_result = decoder.load(entry.adx_data);
        if (!load_result) {
            return std::unexpected(
                "AAX segment " + std::to_string(i) + " is not a valid ADX payload: " + load_result.error());
        }

        const auto& header = decoder.header();
        if (i == 0) {
            expected_channels = header.channels;
            expected_sample_rate = header.sample_rate;
        } else if (header.channels != expected_channels || header.sample_rate != expected_sample_rate) {
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
    auto bytes = build(entries);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    if (output_path.has_parent_path()) {
        std::error_code filesystem_error;
        std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
        if (filesystem_error) {
            return std::unexpected("AAX build failed: could not create output directory: " + filesystem_error.message());
        }
    }

    io::writer writer;
    if (auto result = writer.open(output_path); !result) {
        return std::unexpected("AAX build failed: could not open output file: " + output_path.string());
    }
    if (auto result = writer.write(*bytes); !result) {
        (void)writer.close();
        return std::unexpected("AAX build failed: could not write output file: " + output_path.string());
    }
    if (auto result = writer.close(); !result) {
        return std::unexpected("AAX build failed: could not finalize output file: " + output_path.string());
    }

    return {};
}

std::expected<void, std::string> AaxContainer::parse() {
    m_segments.clear();
    m_looped_segment_data.clear();
    m_channels = 0;
    m_sample_rate = 0;
    m_sample_count = 0;

    if (m_table.row_count() == 0) {
        return std::unexpected("AAX table has no rows");
    }

    bool found_non_empty_segment = false;

    for (uint32_t i = 0; i < m_table.row_count(); ++i) {
        auto loop_flag = require_value<uint8_t>(m_table, i, "lpflg");
        if (!loop_flag) {
            return std::unexpected(loop_flag.error());
        }

        auto data = require_data(m_table, i, "data");
        if (!data) {
            return std::unexpected(data.error());
        }

        uint32_t segment_sample_count = 0;
        if (!data->empty()) {
            found_non_empty_segment = true;

            adx::AdxDecoder decoder;
            auto load_result = decoder.load(*data);
            if (!load_result) {
                return std::unexpected(
                    "AAX segment " + std::to_string(i) + " is not a valid ADX payload: " + load_result.error());
            }

            const auto& header = decoder.header();
            if (m_channels == 0) {
                m_channels = header.channels;
                m_sample_rate = header.sample_rate;
            } else if (header.channels != m_channels || header.sample_rate != m_sample_rate) {
                return std::unexpected("AAX segments must share the same channel count and sample rate");
            }

            segment_sample_count = header.sample_count;
            m_sample_count += segment_sample_count;
        }

        AaxSegmentInfo info;
        info.row_index = i;
        info.data_size = static_cast<uint32_t>(data->size());
        info.sample_count = segment_sample_count;
        info.loop_segment = (*loop_flag != 0);

        m_segments.push_back(std::move(info));
    }

    if (!found_non_empty_segment) {
        return std::unexpected("AAX contains no ADX segment data");
    }

    m_looped_segment_data.resize(m_segments.size());
    return {};
}

bool AaxContainer::has_loop_segments() const noexcept {
    return std::ranges::any_of(m_segments, &AaxSegmentInfo::loop_segment);
}

std::expected<std::span<const uint8_t>, std::string> AaxContainer::raw_segment_data(uint32_t index) const {
    if (index >= m_segments.size()) {
        return std::unexpected("AAX segment index is out of range");
    }

    auto data = m_table.get_data(index, "data");
    if (!data) {
        return std::unexpected(
            "Missing or invalid 'data' at row " + std::to_string(index) + ": " + data.error());
    }

    return *data;
}

std::expected<std::span<const uint8_t>, std::string> AaxContainer::looped_segment_data(uint32_t index) const {
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

    if (!m_looped_segment_data[index]) {
        auto decoded = decoder.decode();
        if (!decoded) {
            return std::unexpected(
                "AAX segment " + std::to_string(index) + " loop projection failed: " + decoded.error());
        }

        const auto& header = decoder.header();
        adx::AdxEncodeConfig config{};
        config.sample_rate = header.sample_rate;
        config.channels = header.channels;
        config.bit_depth = header.bit_depth;
        config.block_size = header.block_size;
        config.encoding_mode = header.encoding_mode;
        config.highpass_freq = header.highpass_freq;
        config.version = header.version;
        config.encryption_type = 0;
        const adx::AdxLoop loop{
            .index = 0,
            .type = 1,
            .start_sample = 0,
            .start_byte = 0,
            .end_sample = decoded->sample_count,
            .end_byte = 0,
        };

        auto encoded = adx::AdxEncoder::encode(decoded->pcm_data, config, std::span<const adx::AdxLoop>(&loop, 1));
        if (!encoded) {
            return std::unexpected(
                "AAX segment " + std::to_string(index) + " loop projection failed: " + encoded.error());
        }
        m_looped_segment_data[index] = std::move(*encoded);
    }

    return std::span<const uint8_t>(*m_looped_segment_data[index]);
}

std::expected<std::span<const uint8_t>, std::string> AaxContainer::segment_data(uint32_t index) const {
    return looped_segment_data(index);
}

std::expected<void, std::string> AaxContainer::extract_file(
    uint32_t index,
    const std::filesystem::path& output_path
) const {
    auto data = segment_data(index);
    if (!data) {
        return std::unexpected(data.error());
    }

    if (output_path.has_parent_path()) {
        std::error_code filesystem_error;
        std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
        if (filesystem_error) {
            return std::unexpected("AAX extract failed: could not create segment output directory: " + filesystem_error.message());
        }
    }

    io::writer writer;
    if (auto result = writer.open(output_path); !result) {
        return std::unexpected("AAX extract failed: could not open segment output: " + output_path.string());
    }
    if (auto result = writer.write(*data); !result) {
        (void)writer.close();
        return std::unexpected("AAX extract failed: could not write segment output: " + output_path.string());
    }
    if (auto result = writer.close(); !result) {
        return std::unexpected("AAX extract failed: could not finalize segment output: " + output_path.string());
    }

    return {};
}

std::expected<std::vector<uint8_t>, std::string> AaxContainer::synthesized_adx_data() const {
    if (!has_loop_segments()) {
        size_t total_size = 0;
        for (uint32_t i = 0; i < segment_count(); ++i) {
            auto segment = raw_segment_data(i);
            if (!segment) {
                return std::unexpected(segment.error());
            }
            total_size += segment->size();
        }

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
    adx::AdxEncodeConfig config{};
    uint32_t accumulated_samples = 0;
    bool configured = false;

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

        if (!configured) {
            const auto& header = decoder.header();
            config.sample_rate = header.sample_rate;
            config.channels = header.channels;
            config.bit_depth = header.bit_depth;
            config.block_size = header.block_size;
            config.encoding_mode = header.encoding_mode;
            config.highpass_freq = header.highpass_freq;
            config.version = header.version;
            config.encryption_type = 0;
            configured = true;
        }

        if (m_segments[i].loop_segment && decoded->sample_count != 0) {
            loops.assign(1, adx::AdxLoop{
                .index = 0,
                .type = 1,
                .start_sample = accumulated_samples,
                .start_byte = 0,
                .end_sample = accumulated_samples + decoded->sample_count,
                .end_byte = 0,
            });
        }

        pcm.insert(pcm.end(), decoded->pcm_data.begin(), decoded->pcm_data.end());
        accumulated_samples += decoded->sample_count;
    }

    if (pcm.empty()) {
        return std::unexpected("AAX ADX export failed: no decoded PCM was produced");
    }

    auto encoded = adx::AdxEncoder::encode(pcm, config, loops);
    if (!encoded) {
        return std::unexpected("AAX ADX export failed: " + encoded.error());
    }
    return *encoded;
}

std::expected<std::vector<uint8_t>, std::string> AaxContainer::adx_data() const {
    return synthesized_adx_data();
}

std::expected<std::vector<uint8_t>, std::string> AaxContainer::save() const {
    auto entries = build_entries();
    if (!entries) {
        return std::unexpected(entries.error());
    }
    return build(*entries);
}

std::expected<void, std::string> AaxContainer::save_to_file(const std::filesystem::path& output_path) const {
    auto bytes = save();
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    if (output_path.has_parent_path()) {
        std::error_code filesystem_error;
        std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
        if (filesystem_error) {
            return std::unexpected("AAX save failed: could not create output directory: " + filesystem_error.message());
        }
    }

    io::writer writer;
    if (auto result = writer.open(output_path); !result) {
        return std::unexpected("AAX save failed: could not open output: " + output_path.string());
    }
    if (auto result = writer.write(*bytes); !result) {
        (void)writer.close();
        return std::unexpected("AAX save failed: could not write output: " + output_path.string());
    }
    if (auto result = writer.close(); !result) {
        return std::unexpected("AAX save failed: could not finalize output: " + output_path.string());
    }

    return {};
}

std::expected<void, std::string> AaxContainer::export_adx(const std::filesystem::path& output_path) const {
    if (output_path.has_parent_path()) {
        std::error_code filesystem_error;
        std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
        if (filesystem_error) {
            return std::unexpected("AAX export failed: could not create output directory: " + filesystem_error.message());
        }
    }

    io::writer writer;
    if (auto result = writer.open(output_path); !result) {
        return std::unexpected("AAX export failed: could not open output: " + output_path.string());
    }
    auto output = adx_data();
    if (!output) {
        (void)writer.close();
        return std::unexpected(output.error());
    }
    if (auto result = writer.write(*output); !result) {
        (void)writer.close();
        return std::unexpected("AAX export failed: could not write output: " + output_path.string());
    }
    if (auto result = writer.close(); !result) {
        return std::unexpected("AAX export failed: could not finalize output: " + output_path.string());
    }

    return {};
}

} // namespace cricodecs::aax
