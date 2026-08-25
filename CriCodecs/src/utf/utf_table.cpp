/**
 * @file utf_table.cpp
 * @brief Shared @UTF table object helpers.
 *
 * The query model follows vgmstream's `cri_utf` reader shape where applicable,
 * while the table object also supports the current CriCodecs builder/mutation
 * surface. Validation against official CRI binaries and samples by Youjose.
 */

#include "utf_table.hpp"

#include "../utilities/io_endian.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace cricodecs::utf {

using io::read_be;

std::string_view UtfTable::string_at(uint32_t offset) const {
    const size_t size = m_data_offset - m_strings_offset;
    if (offset >= size) {
        return "";
    }
    const char* begin = reinterpret_cast<const char*>(m_source.data() + m_strings_offset + offset);
    const size_t remaining = size - offset;
    const auto* end = static_cast<const char*>(std::memchr(begin, '\0', remaining));
    const size_t len = end == nullptr ? remaining : static_cast<size_t>(end - begin);
    return {begin, len};
}

int UtfTable::find_column(std::string_view name) const {
    auto it = m_column_cache.find(name);
    if (it != m_column_cache.end()) {
        return it->second;
    }

    for (size_t i = 0; i < m_columns.size(); ++i) {
        if (m_columns[i].name == name) {
            m_column_cache.emplace(std::string(name), static_cast<int>(i));
            return static_cast<int>(i);
        }
    }
    m_column_cache.emplace(std::string(name), -1);
    return -1;
}

void UtfTable::set_table_name(std::string_view name) {
    make_editable();
    m_table_name = std::string(name);
}

void UtfTable::make_editable() {
    if (is_loaded()) *this = editable_copy();
}

bool UtfTable::rename_column(uint32_t col, std::string_view name) {
    if (col >= m_columns.size()) {
        return false;
    }
    make_editable();
    m_columns[col].name = name;
    m_column_cache.clear();
    return true;
}

bool UtfTable::set_column_type(uint32_t col, ColumnType type) {
    if (col >= m_columns.size() || get_type_size(type) == 0) {
        return false;
    }
    make_editable();
    if (m_columns[col].type == type) {
        return true;
    }
    m_columns[col].type = type;
    m_default_values[col] = std::monostate{};
    for (auto& row : m_values) {
        row[col] = std::monostate{};
    }
    return true;
}

bool UtfTable::set_column_flag(uint32_t col, ColumnFlag flag) {
    if (col >= m_columns.size() || !has_flag(flag, ColumnFlag::Name)) {
        return false;
    }
    make_editable();
    m_columns[col].flag = flag;
    return true;
}

bool UtfTable::remove_row(uint32_t row) {
    if (row >= row_count()) {
        return false;
    }
    make_editable();
    m_values.erase(m_values.begin() + row);
    return true;
}

bool UtfTable::move_row(uint32_t from_row, uint32_t to_row) {
    if (from_row >= row_count() || to_row >= row_count()) {
        return false;
    }
    make_editable();
    if (from_row < to_row) {
        std::rotate(
            m_values.begin() + from_row,
            m_values.begin() + from_row + 1,
            m_values.begin() + to_row + 1);
    } else if (from_row > to_row) {
        std::rotate(
            m_values.begin() + to_row,
            m_values.begin() + from_row,
            m_values.begin() + from_row + 1);
    }
    return true;
}

bool UtfTable::remove_column(uint32_t col) {
    if (col >= m_columns.size()) {
        return false;
    }
    make_editable();
    m_columns.erase(m_columns.begin() + col);
    m_default_values.erase(m_default_values.begin() + col);
    for (auto& row : m_values) {
        row.erase(row.begin() + col);
    }
    m_column_cache.clear();
    return true;
}

Value UtfTable::read_value_at(const uint8_t* buf, ColumnType type) const {
    switch (type) {
        case ColumnType::UInt8:  return static_cast<uint8_t>(buf[0]);
        case ColumnType::SInt8:  return static_cast<int8_t>(buf[0]);
        case ColumnType::UInt16: return read_be<uint16_t>(buf);
        case ColumnType::SInt16: return read_be<int16_t>(buf);
        case ColumnType::UInt32: return read_be<uint32_t>(buf);
        case ColumnType::SInt32: return read_be<int32_t>(buf);
        case ColumnType::UInt64: return read_be<uint64_t>(buf);
        case ColumnType::SInt64: return read_be<int64_t>(buf);
        case ColumnType::Float:  return read_be<float>(buf);
        case ColumnType::Double: return read_be<double>(buf);
        case ColumnType::String: return std::string(string_at(read_be<uint32_t>(buf)));
        case ColumnType::VLData: return read_be<DataRef>(buf);
        case ColumnType::GUID: {
            GUID guid;
            std::memcpy(guid.data, buf, 16);
            return guid;
        }
        default:
            return std::monostate{};
    }
}

std::expected<std::span<const uint8_t>, std::string> UtfTable::field_data(uint32_t row, uint32_t col) const {
    if (col >= m_columns.size()) {
        return std::unexpected("UTF column index is out of range");
    }
    if (row >= row_count()) {
        return std::unexpected("UTF row index is out of range");
    }

    const Column& column = m_columns[col];
    const uint32_t field_size = get_type_size(column.type);

    if (has_flag(column.flag, ColumnFlag::Row)) {
        const size_t row_offset = m_rows_offset + static_cast<size_t>(row) * m_row_width + column.row_offset;
        if (row_offset > m_strings_offset || field_size > m_strings_offset - row_offset) {
            return std::unexpected("UTF row data is out of bounds");
        }
        return std::span<const uint8_t>(m_source.data() + row_offset, field_size);
    }

    if (has_flag(column.flag, ColumnFlag::Default)) {
        if (!is_loaded()) {
            return std::unexpected("UTF schema data is unavailable");
        }
        const size_t offset = HEADER_SIZE + column.default_offset;
        if (offset > m_rows_offset || field_size > m_rows_offset - offset) {
            return std::unexpected("UTF default value is out of bounds");
        }
        return std::span<const uint8_t>(m_source.data() + offset, field_size);
    }

    return std::unexpected("UTF column has no data");
}

std::expected<Value, std::string> UtfTable::get_value(uint32_t row, uint32_t col) const {
    if (col >= m_columns.size()) {
        return std::unexpected("UTF column index is out of range");
    }
    if (row >= row_count()) {
        return std::unexpected("UTF row index is out of range");
    }
    if (
        row < m_values.size() &&
        col < m_values[row].size() &&
        !std::holds_alternative<std::monostate>(m_values[row][col])
    ) {
        return m_values[row][col];
    }

    auto field = field_data(row, col);
    if (!field) {
        if (field.error() == "UTF column has no data") {
            return Value{std::monostate{}};
        }
        return std::unexpected(field.error());
    }

    return read_value_at(field->data(), m_columns[col].type);
}

std::expected<Value, std::string> UtfTable::get_default_value(uint32_t col) const {
    if (col >= m_columns.size()) {
        return std::unexpected("UTF column index is out of range");
    }

    const Column& column = m_columns[col];
    if (!has_flag(column.flag, ColumnFlag::Default)) {
        return std::unexpected("UTF column has no default value");
    }

    if (col < m_default_values.size() && !std::holds_alternative<std::monostate>(m_default_values[col])) {
        return m_default_values[col];
    }

    if (is_loaded()) {
        return read_value_at(m_source.data() + HEADER_SIZE + column.default_offset, column.type);
    }

    return std::monostate{};
}

std::expected<std::span<const uint8_t>, std::string> UtfTable::data_at(DataRef ref) const {
    const size_t offset = m_data_offset + static_cast<size_t>(ref.offset);
    if (offset > m_table_size || ref.size > m_table_size - offset) {
        return std::unexpected("UTF data reference is out of bounds");
    }
    return std::span<const uint8_t>(m_source.data() + offset, ref.size);
}

std::expected<std::span<const uint8_t>, std::string> UtfTable::get_data(uint32_t row, uint32_t col) const {
    if (
        row < m_values.size() &&
        col < m_values[row].size() &&
        std::holds_alternative<std::vector<uint8_t>>(m_values[row][col])
    ) {
        const auto& bytes = std::get<std::vector<uint8_t>>(m_values[row][col]);
        return std::span<const uint8_t>(bytes.data(), bytes.size());
    }

    auto val = get_value(row, col);
    if (!val) return std::unexpected(val.error());

    if (!std::holds_alternative<DataRef>(*val)) {
        return std::unexpected("UTF column is not VLData type");
    }

    return data_at(std::get<DataRef>(*val));
}

std::expected<std::span<const uint8_t>, std::string> UtfTable::get_default_data(uint32_t col) const {
    if (col < m_default_values.size() &&
        std::holds_alternative<std::vector<uint8_t>>(m_default_values[col])) {
        const auto& bytes = std::get<std::vector<uint8_t>>(m_default_values[col]);
        return std::span<const uint8_t>(bytes);
    }

    auto val = get_default_value(col);
    if (!val) return std::unexpected(val.error());

    if (!std::holds_alternative<DataRef>(*val)) {
        return std::unexpected("UTF column is not VLData type");
    }
    return data_at(std::get<DataRef>(*val));
}

std::expected<std::string_view, std::string> UtfTable::get_string(uint32_t row, uint32_t col) const {
    if (col >= m_columns.size()) {
        return std::unexpected("UTF column index is out of range");
    }
    if (row >= row_count()) {
        return std::unexpected("UTF row index is out of range");
    }
    if (m_columns[col].type != ColumnType::String) {
        return std::unexpected("UTF column is not String type");
    }
    if (
        row < m_values.size() &&
        col < m_values[row].size() &&
        std::holds_alternative<std::string>(m_values[row][col])
    ) {
        return std::string_view(std::get<std::string>(m_values[row][col]));
    }

    auto field = field_data(row, col);
    if (!field) {
        return std::unexpected(field.error());
    }

    const uint32_t str_offset = read_be<uint32_t>(field->data());
    return string_at(str_offset);
}

} // namespace cricodecs::utf
