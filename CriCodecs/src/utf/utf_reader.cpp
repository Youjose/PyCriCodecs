/**
 * @file utf_reader.cpp
 * @brief Generic clear @UTF table reader.
 *
 * The generic reader shape follows vgmstream's `cri_utf` model where
 * applicable, then is checked against official CRI UTF Retriever paths in CPK
 * Maker, Medianoche, AtomCraft, and the official binaries. C++23
 * implementation by Youjose.
 */

#include "utf_table.hpp"
#include "utf_format.hpp"

#include "../utilities/io_endian.hpp"

#include <algorithm>
#include <utility>

namespace cricodecs::utf {

using io::read_be;

std::expected<UtfTable, std::string> UtfTable::load(std::span<const uint8_t> data) {
    return load(data, nullptr);
}

std::expected<UtfTable, std::string> UtfTable::load(std::vector<uint8_t>&& data) {
    auto source = io::SourceView::from_owned(std::move(data));
    return load(source.bytes, std::move(source.owner));
}

std::expected<UtfTable, std::string> UtfTable::load(std::span<const uint8_t> data, io::SourceView::Owner owner) {
    if (data.size() < HEADER_SIZE) {
        return std::unexpected("UTF parse failed: data is too small for header");
    }

    UtfTable table;
    table.m_source = io::SourceView(data, std::move(owner));
    if (auto result = table.parse(); !result) return std::unexpected(result.error());
    return table;
}

std::expected<UtfTable, std::string> UtfTable::load(const std::filesystem::path& path) {
    auto source = io::SourceView::from_file(path);
    if (!source) {
        return std::unexpected("UTF load failed: failed to open " + path.string() + " (" + source.error() + ")");
    }
    return load(source->bytes, std::move(source->owner));
}

std::expected<void, std::string> UtfTable::parse() {
    const uint8_t* buf = m_source.data();

    const auto header = read_be<detail::UtfHeader>(buf);
    if (header.magic != MAGIC_UTF) {
        return std::unexpected("UTF parse failed: invalid magic");
    }

    const uint64_t table_size = static_cast<uint64_t>(header.table_size) + 0x08;
    const uint64_t rows_offset = static_cast<uint64_t>(header.rows_offset) + 0x08;
    const uint64_t strings_offset = static_cast<uint64_t>(header.strings_offset) + 0x08;
    const uint64_t data_offset = static_cast<uint64_t>(header.data_offset) + 0x08;
    m_version = header.version;
    m_row_width = header.row_width;
    m_loaded_row_count = header.row_count;

    if (m_version != 0x00 && m_version != 0x01) {
        return std::unexpected("UTF parse failed: unknown version: " + std::to_string(m_version));
    }

    if (table_size > m_source.size() || table_size > UINT32_MAX) {
        return std::unexpected("UTF parse failed: table size exceeds data size");
    }
    if (rows_offset < HEADER_SIZE || rows_offset > strings_offset ||
        strings_offset > data_offset || data_offset > table_size || data_offset > UINT32_MAX) {
        return std::unexpected("UTF parse failed: invalid section offsets");
    }

    m_table_size = static_cast<uint32_t>(table_size);
    m_rows_offset = static_cast<uint32_t>(rows_offset);
    m_strings_offset = static_cast<uint32_t>(strings_offset);
    m_data_offset = static_cast<uint32_t>(data_offset);

    const uint32_t strings_size = m_data_offset - m_strings_offset;

    if (strings_size == 0 || header.name_offset >= strings_size) {
        return std::unexpected("UTF parse failed: invalid string table");
    }
    if (header.column_count == 0) {
        return std::unexpected("UTF parse failed: table has no columns");
    }

    m_table_name = string_at(header.name_offset);
    m_columns.reserve(header.column_count);

    uint32_t pos = HEADER_SIZE;
    uint32_t column_offset = 0;

    for (uint16_t i = 0; i < header.column_count; ++i) {
        if (pos + 5 > m_rows_offset) {
            return std::unexpected("UTF parse failed: schema ended before column " + std::to_string(i));
        }

        uint8_t info = buf[pos];
        uint8_t flag_byte = info & 0xF0;
        uint8_t type_byte = info & 0x0F;

        ColumnFlag flag = static_cast<ColumnFlag>(flag_byte);
        ColumnType type = static_cast<ColumnType>(type_byte);

        if (flag_byte == 0 || !has_flag(flag, ColumnFlag::Name)) {
            return std::unexpected("UTF parse failed: invalid column flag at column " + std::to_string(i));
        }

        const uint32_t column_name_offset = read_be<uint32_t>(buf + pos + 1);
        if (column_name_offset >= strings_size) {
            return std::unexpected("UTF parse failed: invalid column name offset");
        }
        pos += 5;

        const uint32_t value_size = get_type_size(type);
        if (value_size == 0) {
            return std::unexpected("UTF parse failed: unknown column type: " + std::to_string(type_byte));
        }

        Column column{
            .name = std::string(string_at(column_name_offset)),
            .type = type,
            .flag = flag,
        };

        if (has_flag(flag, ColumnFlag::Default)) {
            if (pos + value_size > m_rows_offset) {
                return std::unexpected("UTF parse failed: default value is out of bounds");
            }
            column.default_offset = pos - HEADER_SIZE;
            pos += value_size;
        }
        if (has_flag(flag, ColumnFlag::Row)) {
            column.row_offset = column_offset;
            column_offset += value_size;
        }

        m_columns.push_back(std::move(column));
    }

    const uint64_t declared_rows_end = static_cast<uint64_t>(m_rows_offset)
        + static_cast<uint64_t>(m_loaded_row_count) * m_row_width;
    const uint64_t last_row_fields_end = m_loaded_row_count == 0
        ? m_rows_offset
        : static_cast<uint64_t>(m_rows_offset)
            + static_cast<uint64_t>(m_loaded_row_count - 1) * m_row_width
            + column_offset;
    if (std::max(declared_rows_end, last_row_fields_end) > m_strings_offset) {
        return std::unexpected("UTF parse failed: row data exceeds row section");
    }

    return {};
}

} // namespace cricodecs::utf
