/**
 * @file utf_builder.cpp
 * @brief Generic clear @UTF table builder.
 *
 * UTF layout behavior is validated against official CRI UTF Maker paths in CPK
 * Maker, Medianoche and AtomCraft. Generic builder
 * implementation by Youjose.
 */

#include "utf_table.hpp"

#include "../utilities/io_endian.hpp"
#include "../utilities/flat_unordered_map.hpp"
#include "../utilities/numeric.hpp"

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstring>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace cricodecs::utf {

namespace {

[[nodiscard]] bool value_matches_type(const Value& value, ColumnType type) noexcept {
    if (std::holds_alternative<std::monostate>(value)) {
        return true;
    }

    switch (type) {
        case ColumnType::UInt8: return std::holds_alternative<uint8_t>(value);
        case ColumnType::SInt8: return std::holds_alternative<int8_t>(value);
        case ColumnType::UInt16: return std::holds_alternative<uint16_t>(value);
        case ColumnType::SInt16: return std::holds_alternative<int16_t>(value);
        case ColumnType::UInt32: return std::holds_alternative<uint32_t>(value);
        case ColumnType::SInt32: return std::holds_alternative<int32_t>(value);
        case ColumnType::UInt64: return std::holds_alternative<uint64_t>(value);
        case ColumnType::SInt64: return std::holds_alternative<int64_t>(value);
        case ColumnType::Float: return std::holds_alternative<float>(value);
        case ColumnType::Double: return std::holds_alternative<double>(value);
        case ColumnType::String: return std::holds_alternative<std::string>(value);
        case ColumnType::VLData:
            return std::holds_alternative<std::vector<uint8_t>>(value) ||
                std::holds_alternative<DataRef>(value);
        case ColumnType::GUID: return std::holds_alternative<GUID>(value);
    }
    return false;
}

} // namespace

using io::write_be;
using util::align_up;

UtfTable UtfTable::create(std::string_view name, uint16_t version) {
    UtfTable table;
    table.m_table_name = name;
    table.m_version = version;
    return table;
}

void UtfTable::add_column(std::string_view name, ColumnType type, ColumnFlag flag) {
    make_editable();
    m_columns.push_back({.name = std::string(name), .type = type, .flag = flag});
    m_default_values.push_back(std::monostate{});
    for (auto& row : m_values) {
        row.resize(m_columns.size(), std::monostate{});
    }
    m_column_cache.clear();
}

uint32_t UtfTable::add_row() {
    make_editable();
    m_values.emplace_back(m_columns.size(), std::monostate{});
    return static_cast<uint32_t>(m_values.size() - 1);
}

std::expected<void, std::string> UtfTable::set(uint32_t row, uint32_t col, Value value) {
    if (row >= row_count()) {
        return std::unexpected("UTF set failed: row index is out of range");
    }
    if (col >= m_columns.size()) {
        return std::unexpected("UTF set failed: column index is out of range");
    }
    if (!value_matches_type(value, m_columns[col].type)) {
        return std::unexpected(
            "UTF set failed: value type does not match column " + m_columns[col].name);
    }
    make_editable();
    if (has_flag(m_columns[col].flag, ColumnFlag::Default) &&
        !has_flag(m_columns[col].flag, ColumnFlag::Row)) {
        for (auto& values : m_values) {
            values[col] = m_default_values[col];
        }
        m_columns[col].flag = ColumnFlag::Name | ColumnFlag::Row;
        m_row_width = 0;
    }
    m_values[row][col] = std::move(value);
    return {};
}

std::expected<void, std::string> UtfTable::set_default_value(uint32_t col, Value value) {
    if (col >= m_columns.size()) {
        return std::unexpected("UTF default set failed: column index is out of range");
    }
    if (!value_matches_type(value, m_columns[col].type)) {
        return std::unexpected(
            "UTF default set failed: value type does not match column " + m_columns[col].name);
    }
    make_editable();
    m_default_values[col] = std::move(value);
    m_columns[col].flag = m_columns[col].flag | ColumnFlag::Default;
    return {};
}

UtfTable UtfTable::editable_copy() const {
    if (!is_loaded()) return *this;

    auto editable = UtfTable::create(m_table_name, m_version);
    editable.set_text_encoding(m_text_encoding);
    if (m_row_width != 0) {
        editable.set_row_width(m_row_width);
    }
    if (m_data_alignment != 0) {
        editable.set_data_alignment(m_data_alignment);
    }

    for (uint32_t column = 0; column < column_count(); ++column) {
        const auto& col = m_columns[column];
        editable.add_column(col.name, col.type, col.flag);
        if (has_flag(col.flag, ColumnFlag::Default)) {
            if (col.type == ColumnType::VLData) {
                if (auto data = get_default_data(column)) {
                    editable.set_default_value(
                        column,
                        std::vector<uint8_t>(data->begin(), data->end())
                    ).value();
                }
            } else if (auto value = get_default_value(column)) {
                editable.set_default_value(column, *value).value();
            }
        }
    }

    for (uint32_t row = 0; row < row_count(); ++row) {
        editable.add_row();
        for (uint32_t column = 0; column < column_count(); ++column) {
            const auto& col = m_columns[column];
            if (!has_flag(col.flag, ColumnFlag::Row)) {
                continue;
            }
            if (col.type == ColumnType::VLData) {
                if (auto data = get_data(row, column)) {
                    editable.set(
                        row,
                        column,
                        std::vector<uint8_t>(data->begin(), data->end())
                    ).value();
                }
            } else if (auto value = get_value(row, column)) {
                editable.set(row, column, *value).value();
            }
        }
    }

    return editable;
}

std::vector<uint8_t> UtfTable::build() const {
    if (is_loaded()) return editable_copy().build();

    const uint32_t rows = row_count();
    std::vector<ColumnFlag> flags;
    flags.reserve(m_columns.size());
    for (size_t column = 0; column < m_columns.size(); ++column) {
        const auto explicit_flags = m_columns[column].flag;
        if (has_flag(explicit_flags, ColumnFlag::Row) || has_flag(explicit_flags, ColumnFlag::Default)) {
            flags.push_back(explicit_flags);
            continue;
        }

        const auto value_at = [column](const auto& row) -> const Value& { return row[column]; };
        const bool empty = std::ranges::all_of(m_values, [](const Value& value) {
            return std::holds_alternative<std::monostate>(value);
        }, value_at);
        if (empty) {
            flags.push_back(ColumnFlag::Name);
            continue;
        }
        if (rows == 1) {
            flags.push_back(ColumnFlag::Name | ColumnFlag::Row);
            continue;
        }

        const Value& first = m_values.front()[column];
        const bool constant = std::ranges::all_of(m_values, [&first](const Value& value) {
            return value == first;
        }, value_at);
        flags.push_back(constant
            ? ColumnFlag::Name | ColumnFlag::Default
            : ColumnFlag::Name | ColumnFlag::Row);
    }

    auto default_value_for_column = [&](size_t column_index) -> const Value* {
        if (column_index < m_default_values.size() &&
            !std::holds_alternative<std::monostate>(m_default_values[column_index])) {
            return &m_default_values[column_index];
        }
        if (!m_values.empty() && column_index < m_values[0].size() &&
            !std::holds_alternative<std::monostate>(m_values[0][column_index])) {
            return &m_values[0][column_index];
        }
        return nullptr;
    };

    cricodecs::util::flat_unordered_map<std::string, uint32_t, cricodecs::util::transparent_string_hash, std::equal_to<>> string_offsets;
    string_offsets.reserve(m_columns.size() + 1);
    std::vector<uint8_t> string_blob;

    auto add_string = [&](std::string_view s) -> uint32_t {
        auto it = string_offsets.find(s);
        if (it != string_offsets.end()) return it->second;

        const uint32_t offset = static_cast<uint32_t>(string_blob.size());
        string_blob.insert(string_blob.end(), s.begin(), s.end());
        string_blob.push_back(0);
        string_offsets.emplace(std::string(s), offset);
        return offset;
    };

    if (m_version == 0) {
        add_string("<NULL>");
    }

    uint32_t table_name_offset = add_string(m_table_name);

    std::vector<uint32_t> column_name_offsets;
    column_name_offsets.reserve(m_columns.size());
    for (const auto& col : m_columns) {
        column_name_offsets.push_back(add_string(col.name));
    }

    for (size_t c = 0; c < m_columns.size(); ++c) {
        if (m_columns[c].type != ColumnType::String || !has_flag(flags[c], ColumnFlag::Default)) {
            continue;
        }

        const Value* default_value = default_value_for_column(c);
        if (default_value != nullptr && std::holds_alternative<std::string>(*default_value)) {
            add_string(std::get<std::string>(*default_value));
        }
    }

    for (size_t c = 0; c < m_columns.size(); ++c) {
        if (m_columns[c].type != ColumnType::String) continue;
        for (uint32_t r = 0; r < rows; ++r) {
            if (std::holds_alternative<std::string>(m_values[r][c])) {
                add_string(std::get<std::string>(m_values[r][c]));
            }
        }
    }

    uint32_t schema_size = 0;
    uint32_t computed_row_width = 0;
    for (size_t c = 0; c < m_columns.size(); ++c) {
        schema_size += 5;
        uint32_t type_size = get_type_size(m_columns[c].type);
        if (has_flag(flags[c], ColumnFlag::Default)) {
            schema_size += type_size;
        }
        if (has_flag(flags[c], ColumnFlag::Row)) {
            computed_row_width += type_size;
        }
    }

    uint32_t header_row_width = (m_row_width > 0) ? m_row_width : computed_row_width;

    uint32_t rows_offset = HEADER_SIZE + schema_size;
    uint32_t rows_size = rows * computed_row_width;

    uint32_t strings_offset = rows_offset + rows_size;
    uint32_t strings_size = static_cast<uint32_t>(string_blob.size());

    std::vector<uint8_t> data_blob;
    std::unordered_map<const Value*, uint32_t> data_offsets;

    auto append_vldata = [&](const Value& value) {
        if (data_offsets.contains(&value)) return;
        const auto& data = std::get<std::vector<uint8_t>>(value);
        if (data.empty()) return;
        if (m_data_alignment > 0 && !data_blob.empty()) {
            // ACB places a gap before each aligned VLData entry.
            const uint32_t aligned = align_up(static_cast<uint32_t>(data_blob.size()) + 1, m_data_alignment);
            data_blob.resize(aligned, 0);
        }
        data_offsets[&value] = static_cast<uint32_t>(data_blob.size());
        data_blob.insert(data_blob.end(), data.begin(), data.end());
    };

    for (size_t c = 0; c < m_columns.size(); ++c) {
        if (m_columns[c].type != ColumnType::VLData || !has_flag(flags[c], ColumnFlag::Default)) {
            continue;
        }
        const Value* default_value = default_value_for_column(c);
        if (default_value != nullptr && std::holds_alternative<std::vector<uint8_t>>(*default_value)) {
            append_vldata(*default_value);
        }
    }

    for (uint32_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < m_columns.size(); ++c) {
            if (m_columns[c].type == ColumnType::VLData &&
                std::holds_alternative<std::vector<uint8_t>>(m_values[r][c])) {
                append_vldata(m_values[r][c]);
            }
        }
    }

    const bool has_data = !data_blob.empty();
    uint32_t data_offset = strings_offset + strings_size;

    if (m_data_alignment > 0 && has_data) {
        data_offset = align_up(data_offset, m_data_alignment);
    }

    uint32_t total_size = data_offset + static_cast<uint32_t>(data_blob.size());
    uint32_t final_align = (m_data_alignment > 0) ? m_data_alignment : 8;
    uint32_t padded_size = align_up(total_size, final_align);

    if (!has_data) {
        data_offset = padded_size;
    }

    std::vector<uint8_t> output(padded_size, 0);
    uint8_t* buf = output.data();

    write_be<uint32_t>(buf + 0x00, MAGIC_UTF);
    write_be<uint32_t>(buf + 0x04, padded_size - 0x08);
    write_be<uint16_t>(buf + 0x08, m_version);
    write_be<uint16_t>(buf + 0x0A, static_cast<uint16_t>(rows_offset - 0x08));
    write_be<uint32_t>(buf + 0x0C, strings_offset - 0x08);
    write_be<uint32_t>(buf + 0x10, data_offset - 0x08);
    write_be<uint32_t>(buf + 0x14, table_name_offset);
    write_be<uint16_t>(buf + 0x18, static_cast<uint16_t>(m_columns.size()));
    write_be<uint16_t>(buf + 0x1A, static_cast<uint16_t>(header_row_width));
    write_be<uint32_t>(buf + 0x1C, rows);

    auto write_value = [&](uint8_t* dst, const Value& val) {
        std::visit([&](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::same_as<T, std::monostate>) {
            } else if constexpr (std::integral<T>) {
                if constexpr (sizeof(T) == 1) dst[0] = static_cast<uint8_t>(v);
                else write_be<T>(dst, v);
            } else if constexpr (std::same_as<T, float>) {
                write_be<uint32_t>(dst, std::bit_cast<uint32_t>(v));
            } else if constexpr (std::same_as<T, double>) {
                write_be<uint64_t>(dst, std::bit_cast<uint64_t>(v));
            } else if constexpr (std::same_as<T, std::string>) {
                write_be<uint32_t>(dst, string_offsets.at(v));
            } else if constexpr (std::same_as<T, std::vector<uint8_t>>) {
                auto it = data_offsets.find(&val);
                uint32_t off = (it != data_offsets.end()) ? it->second : 0;
                write_be<uint32_t>(dst, off);
                write_be<uint32_t>(dst + 4, static_cast<uint32_t>(v.size()));
            } else if constexpr (std::same_as<T, GUID>) {
                std::memcpy(dst, v.data, 16);
            } else if constexpr (std::same_as<T, DataRef>) {
                write_be<uint32_t>(dst, v.offset);
                write_be<uint32_t>(dst + 4, v.size);
            }
        }, val);
    };

    uint32_t schema_pos = HEADER_SIZE;
    for (size_t c = 0; c < m_columns.size(); ++c) {
        uint8_t info = static_cast<uint8_t>(flags[c]) | static_cast<uint8_t>(m_columns[c].type);
        buf[schema_pos++] = info;
        write_be<uint32_t>(buf + schema_pos, column_name_offsets[c]);
        schema_pos += 4;

        if (has_flag(flags[c], ColumnFlag::Default)) {
            if (const Value* default_value = default_value_for_column(c); default_value != nullptr) {
                write_value(buf + schema_pos, *default_value);
            }
            schema_pos += get_type_size(m_columns[c].type);
        }
    }

    uint32_t row_pos = rows_offset;
    for (uint32_t r = 0; r < rows; ++r) {
        uint32_t col_pos = row_pos;
        for (size_t c = 0; c < m_columns.size(); ++c) {
            if (!has_flag(flags[c], ColumnFlag::Row)) continue;
            write_value(buf + col_pos, m_values[r][c]);
            col_pos += get_type_size(m_columns[c].type);
        }
        row_pos += computed_row_width;
    }

    std::memcpy(buf + strings_offset, string_blob.data(), string_blob.size());

    if (!data_blob.empty()) std::memcpy(buf + data_offset, data_blob.data(), data_blob.size());

    return output;
}

} // namespace cricodecs::utf
