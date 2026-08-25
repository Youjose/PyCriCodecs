/**
 * @file csb_reader.cpp
 * @brief CSB UTF cue archive reader.
 *
 * Reader behavior follows vgmstream's CSB loader model, then narrows it
 * against `TBLCSB`, `TBLSDL`, and embedded wrapper tables. C++23 reader
 * implementation by Youjose.
 */

#include "csb_container.hpp"

#include <algorithm>

#include "csb_format.hpp"
#include "../utilities/io.hpp"

namespace cricodecs::csb {

namespace {

using detail::decode_cri_string;
using detail::parse_wrapper_table;
using detail::parse_wrapper_table_name;
using detail::read_segment_loop_flag;
using detail::require_data;
using detail::require_string;
using detail::require_value;
using detail::validate_wrapper_table_name;

std::expected<std::vector<std::span<const uint8_t>>, std::string> wrapper_payloads(
    std::span<const uint8_t> wrapper
) {
    auto table = parse_wrapper_table(wrapper);
    if (!table) {
        return std::unexpected(table.error());
    }
    if (table->row_count() == 0) {
        return std::unexpected("CSB export failed: wrapped UTF payload has no rows");
    }

    if (table->table_name() == "ADPCM_WII") {
        if (table->row_count() != 1) {
            return std::unexpected("CSB export failed: ADPCM_WII wrapper must contain exactly one row");
        }

        auto header = require_data(*table, 0, "header", "ADPCM_WII wrapper");
        if (!header) return std::unexpected(header.error());
        auto data = require_data(*table, 0, "data", "ADPCM_WII wrapper");
        if (!data) return std::unexpected(data.error());
        return std::vector<std::span<const uint8_t>>{*header, *data};
    }

    std::vector<std::span<const uint8_t>> payloads;
    payloads.reserve(table->row_count());
    const bool validate_loop_flags =
        (table->table_name() == "AAX" || table->table_name() == "HCA") &&
        table->find_column("lpflg") >= 0;

    for (uint32_t i = 0; i < table->row_count(); ++i) {
        if (validate_loop_flags) {
            auto loop_flag = read_segment_loop_flag(*table, i);
            if (!loop_flag) {
                return std::unexpected(loop_flag.error());
            }
        }

        auto payload = require_data(*table, i, "data", "Wrapped UTF payload");
        if (!payload) {
            return std::unexpected(payload.error());
        }
        payloads.push_back(*payload);
    }

    return payloads;
}

} // namespace

std::expected<CsbContainer, std::string> CsbContainer::load(
    const std::filesystem::path& path,
    const text::EncodingOptions& encoding
) {
    auto source = io::SourceView::from_file(path);
    if (!source) {
        return std::unexpected("CSB load failed: " + path.string() + " (" + source.error() + ")");
    }
    return load_source(std::move(*source), encoding)
        .transform([&](CsbContainer csb) {
            csb.m_source_path = path;
            return csb;
        });
}

std::expected<CsbContainer, std::string> CsbContainer::load(
    std::vector<uint8_t>&& data,
    const text::EncodingOptions& encoding
) {
    return load_source(io::SourceView::from_owned(std::move(data)), encoding);
}

std::expected<CsbContainer, std::string> CsbContainer::load_source(
    io::SourceView source,
    const text::EncodingOptions& encoding
) {
    CsbContainer csb;
    csb.m_encoding = encoding;
    csb.m_source = std::move(source);

    auto header = utf::UtfTable::load(csb.m_source.bytes);
    if (!header) {
        return std::unexpected("CSB load failed: could not parse root table: " + header.error());
    }
    if (header->table_name() != "TBLCSB") {
        return std::unexpected("CSB load failed: expected root table name TBLCSB");
    }

    csb.m_header = std::move(*header);

    auto parse_result = csb.parse();
    if (!parse_result) {
        return std::unexpected(parse_result.error());
    }

    return csb;
}

std::expected<CsbContainer, std::string> CsbContainer::load(
    std::span<const uint8_t> data,
    const text::EncodingOptions& encoding
) {
    return load_source(
        io::SourceView::from_owned(std::vector<uint8_t>(data.begin(), data.end())),
        encoding);
}

std::expected<void, std::string> CsbContainer::parse() {
    if (auto result = parse_sections(); !result) return result;
    if (auto result = parse_sound_elements(); !result) return result;
    return {};
}

std::expected<void, std::string> CsbContainer::parse_sections() {
    m_sections.clear();

    int sound_element_row = -1;

    for (uint32_t i = 0; i < m_header.row_count(); ++i) {
        auto section_name = require_string(m_header, i, "name");
        if (!section_name) return std::unexpected(section_name.error());

        auto table_type = require_value<uint8_t>(m_header, i, "ttype");
        if (!table_type) return std::unexpected(table_type.error());

        CsbSection section;
        section.row_index = i;
        section.name = std::move(*section_name);
        section.table_type = *table_type;

        auto data = require_data(m_header, i, "utf", "CSB section");
        if (!data) return std::unexpected(data.error());
        section.data_size = static_cast<uint32_t>(data->size());

        if (section.name == "SOUND_ELEMENT") {
            sound_element_row = static_cast<int>(i);
        }

        m_sections.push_back(std::move(section));
    }

    if (sound_element_row < 0) {
        return std::unexpected("CSB is missing the SOUND_ELEMENT section");
    }

    const auto& sound_section = m_sections[static_cast<size_t>(sound_element_row)];
    if (sound_section.table_type != 4) {
        return std::unexpected("CSB parse failed: SOUND_ELEMENT section has an unexpected table type");
    }

    auto sound_data = require_data(
        m_header,
        static_cast<uint32_t>(sound_element_row),
        "utf",
        "SOUND_ELEMENT section"
    );
    if (!sound_data) {
        return std::unexpected(sound_data.error());
    }
    if (sound_data->empty()) {
        return std::unexpected("CSB parse failed: SOUND_ELEMENT section has no embedded UTF data");
    }

    auto sound_table = utf::UtfTable::load(*sound_data);
    if (!sound_table) {
        return std::unexpected("CSB parse failed: could not parse SOUND_ELEMENT table: " + sound_table.error());
    }
    if (sound_table->table_name() != "TBLSDL") {
        return std::unexpected("CSB parse failed: expected SOUND_ELEMENT table name TBLSDL");
    }

    m_sound_element = std::move(*sound_table);
    return {};
}

std::expected<void, std::string> CsbContainer::parse_sound_elements() {
    m_elements.clear();
    m_embedded_indices.clear();

    for (uint32_t i = 0; i < m_sound_element.row_count(); ++i) {
        auto name = require_string(m_sound_element, i, "name");
        if (!name) return std::unexpected(name.error());

        auto format = require_value<uint8_t>(m_sound_element, i, "fmt");
        if (!format) return std::unexpected(format.error());

        auto channels = require_value<uint8_t>(m_sound_element, i, "nch");
        if (!channels) return std::unexpected(channels.error());

        auto sample_rate = require_value<uint32_t>(m_sound_element, i, "sfreq");
        if (!sample_rate) return std::unexpected(sample_rate.error());

        auto sample_count = require_value<uint32_t>(m_sound_element, i, "nsmpl");
        if (!sample_count) return std::unexpected(sample_count.error());

        auto streamed = require_value<uint8_t>(m_sound_element, i, "stmflg");
        if (!streamed) return std::unexpected(streamed.error());

        auto wrapper = require_data(m_sound_element, i, "data", "SOUND_ELEMENT row");
        if (!wrapper) return std::unexpected(wrapper.error());

        auto wrapper_table_name = parse_wrapper_table_name(*wrapper);
        if (!wrapper_table_name) return std::unexpected(wrapper_table_name.error());
        if (auto validate = validate_wrapper_table_name(*format, *wrapper_table_name); !validate) {
            return std::unexpected(validate.error());
        }

        CsbStreamInfo info;
        info.row_index = i;
        info.name_raw = std::move(*name);
        auto decoded_name = decode_cri_string(info.name_raw, m_encoding, "CSB SOUND_ELEMENT name decode failed");
        if (!decoded_name) {
            return std::unexpected(decoded_name.error());
        }
        info.name = std::move(*decoded_name);
        info.format = *format;
        info.wrapper_table_name = std::move(*wrapper_table_name);
        info.channels = *channels;
        info.sample_rate = *sample_rate;
        info.sample_count = *sample_count;
        info.streamed = (*streamed != 0);
        info.wrapper_size = static_cast<uint32_t>(wrapper->size());

        m_elements.push_back(std::move(info));

        if (!m_elements.back().streamed) {
            if (m_elements.back().wrapper_size == 0) {
                return std::unexpected(
                    "Embedded SOUND_ELEMENT row " + std::to_string(i) + " has no wrapper payload");
            }
            m_embedded_indices.push_back(i);
        }
    }

    if (m_embedded_indices.empty()) {
        return std::unexpected("CSB contains no embedded SOUND_ELEMENT streams");
    }

    return {};
}

std::expected<std::span<const uint8_t>, std::string> CsbContainer::wrapper_data(uint32_t index) const {
    if (index >= m_embedded_indices.size()) {
        return std::unexpected("CSB stream index is out of range");
    }

    const uint32_t row_index = m_embedded_indices[index];
    auto data = require_data(m_sound_element, row_index, "data", "SOUND_ELEMENT row");
    if (!data) {
        return std::unexpected(data.error());
    }
    if (data->empty()) {
        return std::unexpected("CSB export failed: selected stream has no wrapper payload");
    }

    return *data;
}

std::expected<utf::UtfTable, std::string> CsbContainer::section_table(uint32_t index) const {
    if (index >= m_sections.size()) {
        return std::unexpected("CSB section index is out of range");
    }

    const auto& section = m_sections[index];
    auto data = require_data(m_header, section.row_index, "utf", "CSB section");
    if (!data) {
        return std::unexpected(data.error());
    }
    if (data->empty()) {
        return std::unexpected("CSB section table is empty: " + section.name);
    }

    auto table = utf::UtfTable::load(*data);
    if (!table) {
        return std::unexpected("CSB section table parse failed for '" + section.name + "': " + table.error());
    }

    return std::move(*table);
}

std::expected<utf::UtfTable, std::string> CsbContainer::wrapper_table(uint32_t index) const {
    auto wrapper = wrapper_data(index);
    if (!wrapper) {
        return std::unexpected(wrapper.error());
    }

    return parse_wrapper_table(*wrapper);
}

std::expected<std::vector<uint8_t>, std::string> CsbContainer::stream_data(uint32_t index) const {
    auto wrapper = wrapper_data(index);
    if (!wrapper) {
        return std::unexpected(wrapper.error());
    }

    if (stream(index).format == 0) {
        return std::vector<uint8_t>(wrapper->begin(), wrapper->end());
    }

    auto payloads = wrapper_payloads(*wrapper);
    if (!payloads) return std::unexpected(payloads.error());

    const auto total_size = std::ranges::fold_left(
        *payloads, size_t{0}, [](size_t size, std::span<const uint8_t> payload) {
            return size + payload.size();
        });
    std::vector<uint8_t> output;
    output.reserve(total_size);
    for (const auto payload : *payloads) {
        output.insert(output.end(), payload.begin(), payload.end());
    }
    return output;
}

std::expected<void, std::string> CsbContainer::export_stream(
    uint32_t index,
    const std::filesystem::path& output_path
) const {
    auto wrapper = wrapper_data(index);
    if (!wrapper) {
        return std::unexpected(wrapper.error());
    }
    const auto& stream_info = stream(index);

    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    io::writer writer;
    if (auto result = writer.open(output_path); !result) {
        return std::unexpected("CSB export failed: could not open output file: " + output_path.string());
    }

    if (stream_info.format == 0) {
        if (auto result = writer.write(*wrapper); !result) {
            (void)writer.close();
            return std::unexpected("CSB export failed: could not write stream data: " + output_path.string());
        }
    } else {
        auto payloads = wrapper_payloads(*wrapper);
        if (!payloads) {
            (void)writer.close();
            return std::unexpected(payloads.error());
        }
        for (const auto payload : *payloads) {
            if (auto result = writer.write(payload); !result) {
                (void)writer.close();
                return std::unexpected("CSB export failed: could not write stream data: " + output_path.string());
            }
        }
    }

    if (auto result = writer.close(); !result) {
        return std::unexpected("CSB export failed: could not finalize output file: " + output_path.string());
    }

    return {};
}

std::expected<void, std::string> CsbContainer::export_all(const std::filesystem::path& output_dir) const {
    for (uint32_t i = 0; i < stream_count(); ++i) {
        auto result = export_stream(i, output_dir / stream(i).suggested_path());
        if (!result) {
            return result;
        }
    }

    return {};
}

} // namespace cricodecs::csb
