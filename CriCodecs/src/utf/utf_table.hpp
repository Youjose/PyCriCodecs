#pragma once
/**
 * @file utf_table.hpp
 * @brief Generic clear @UTF table API.
 *
 * The schema/value model follows vgmstream's `cri_utf` reader shape where
 * applicable and is checked against official CRI UTF Maker/Retriever behavior
 * observed in CPK, USM, and ACB tooling. Public C++23 API by Youjose.
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <expected>
#include <variant>
#include <optional>
#include <bit>
#include <concepts>
#include <utility>

#include "../utilities/flat_unordered_map.hpp"
#include "../utilities/io_reader.hpp"

namespace cricodecs::utf {

enum class ColumnFlag : uint8_t {
    Name     = 0x10,
    Default  = 0x20,
    Row      = 0x40,
};

inline ColumnFlag operator|(ColumnFlag a, ColumnFlag b) {
    return static_cast<ColumnFlag>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline ColumnFlag operator&(ColumnFlag a, ColumnFlag b) {
    return static_cast<ColumnFlag>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline bool has_flag(ColumnFlag flags, ColumnFlag flag) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

enum class ColumnType : uint8_t {
    UInt8   = 0x00,  SInt8   = 0x01,
    UInt16  = 0x02,  SInt16  = 0x03,
    UInt32  = 0x04,  SInt32  = 0x05,
    UInt64  = 0x06,  SInt64  = 0x07,
    Float   = 0x08,  Double  = 0x09,
    String  = 0x0A,  VLData  = 0x0B,
    GUID    = 0x0C,
};

constexpr uint32_t get_type_size(ColumnType type) {
    switch (type) {
        case ColumnType::UInt8:
        case ColumnType::SInt8:  return 1;
        case ColumnType::UInt16:
        case ColumnType::SInt16: return 2;
        case ColumnType::UInt32:
        case ColumnType::SInt32:
        case ColumnType::Float:
        case ColumnType::String: return 4;
        case ColumnType::UInt64:
        case ColumnType::SInt64:
        case ColumnType::Double:
        case ColumnType::VLData: return 8;
        case ColumnType::GUID:   return 16;
        default: return 0;
    }
}

struct DataRef {
    uint32_t offset = 0;
    uint32_t size = 0;
    bool operator==(const DataRef&) const = default;
};

struct GUID {
    uint8_t data[16] = {};
    bool operator==(const GUID&) const = default;
};

using Value = std::variant<
    std::monostate,
    uint8_t, int8_t,
    uint16_t, int16_t,
    uint32_t, int32_t,
    uint64_t, int64_t,
    float, double,
    std::string,
    std::vector<uint8_t>,
    DataRef,
    GUID
>;

struct Column {
    std::string name;
    ColumnType type;
    ColumnFlag flag;
    uint32_t default_offset = 0;
    uint32_t row_offset = 0;
};

class UtfTable {
public:
    static constexpr uint32_t MAGIC_UTF = 0x40555446;
    static constexpr uint32_t HEADER_SIZE = 0x20;
    
    UtfTable() = default;
    
    static std::expected<UtfTable, std::string> load(std::span<const uint8_t> data);
    static std::expected<UtfTable, std::string> load(std::vector<uint8_t>&& data);
    static std::expected<UtfTable, std::string> load(std::span<const uint8_t> data, io::SourceView::Owner owner);
    static std::expected<UtfTable, std::string> load(const std::filesystem::path& path);
    
    std::string_view table_name() const { return m_table_name; }
    std::string_view table_name_bytes() const { return m_table_name; }
    uint32_t row_count() const {
        return is_loaded() ? m_loaded_row_count : static_cast<uint32_t>(m_values.size());
    }
    uint32_t column_count() const { return static_cast<uint32_t>(m_columns.size()); }
    const Column& column(uint32_t index) const { return m_columns[index]; }
    const std::vector<Column>& columns() const { return m_columns; }
    uint16_t version() const { return m_version; }
    uint32_t table_size() const { return m_table_size; }
    uint32_t data_offset() const { return m_data_offset; }
    const std::optional<std::string>& text_encoding() const { return m_text_encoding; }
    void set_text_encoding(std::optional<std::string> encoding) { m_text_encoding = std::move(encoding); }
    void set_table_name(std::string_view name);
    uint16_t row_width() const { return m_row_width; }
    void set_row_width(uint16_t w) { m_row_width = w; }
    
    // ACB callers use 32; generic UTF only aligns the table to 8 bytes.
    uint32_t data_alignment() const { return m_data_alignment; }
    void set_data_alignment(uint32_t alignment) { m_data_alignment = alignment; }
    
    int find_column(std::string_view name) const;
    
    template<typename T>
    std::expected<T, std::string> get(uint32_t row, uint32_t col) const {
        constexpr auto expected_type = [] {
            if constexpr (std::same_as<T, uint8_t>) return ColumnType::UInt8;
            else if constexpr (std::same_as<T, int8_t>) return ColumnType::SInt8;
            else if constexpr (std::same_as<T, uint16_t>) return ColumnType::UInt16;
            else if constexpr (std::same_as<T, int16_t>) return ColumnType::SInt16;
            else if constexpr (std::same_as<T, uint32_t>) return ColumnType::UInt32;
            else if constexpr (std::same_as<T, int32_t>) return ColumnType::SInt32;
            else if constexpr (std::same_as<T, uint64_t>) return ColumnType::UInt64;
            else if constexpr (std::same_as<T, int64_t>) return ColumnType::SInt64;
            else if constexpr (std::same_as<T, float>) return ColumnType::Float;
            else if constexpr (std::same_as<T, double>) return ColumnType::Double;
        }();

        if (col >= m_columns.size()) return std::unexpected("UTF column index is out of range");
        if (row >= row_count()) return std::unexpected("UTF row index is out of range");
        if (m_columns[col].type != expected_type) {
            return std::unexpected("UTF value read failed: type mismatch");
        }

        if (row < m_values.size() && col < m_values[row].size()) {
            if (const auto* number = std::get_if<T>(&m_values[row][col])) return *number;
        }

        const auto field = field_data(row, col);
        if (!field && has_flag(m_columns[col].flag, ColumnFlag::Default)) {
            if (col < m_default_values.size()) {
                if (const auto* number = std::get_if<T>(&m_default_values[col])) return *number;
            }
            if (field.error() == "UTF column has no data" ||
                field.error() == "UTF schema data is unavailable" ||
                field.error() == "UTF default value is out of bounds") {
                return std::unexpected("UTF value read failed: type mismatch");
            }
        }
        if (!field) return std::unexpected(field.error());

        if constexpr (std::same_as<T, uint8_t> || std::same_as<T, int8_t>) {
            return static_cast<T>((*field)[0]);
        } else if constexpr (std::same_as<T, float>) {
            return std::bit_cast<float>(io::read_be<uint32_t>(field->data()));
        } else if constexpr (std::same_as<T, double>) {
            return std::bit_cast<double>(io::read_be<uint64_t>(field->data()));
        } else {
            return io::read_be<T>(field->data());
        }
    }
    
    template<typename T>
    std::expected<T, std::string> get(uint32_t row, std::string_view col_name) const {
        int col = find_column(col_name);
        if (col < 0) return std::unexpected("UTF column not found: " + std::string(col_name));
        return get<T>(row, static_cast<uint32_t>(col));
    }
    
    std::expected<Value, std::string> get_value(uint32_t row, uint32_t col) const;
    std::expected<Value, std::string> get_default_value(uint32_t col) const;
    std::expected<Value, std::string> get_default_value(std::string_view col_name) const {
        int col = find_column(col_name);
        if (col < 0) return std::unexpected("UTF column not found: " + std::string(col_name));
        return get_default_value(static_cast<uint32_t>(col));
    }
    
    std::expected<std::span<const uint8_t>, std::string> get_data(uint32_t row, uint32_t col) const;
    std::expected<std::span<const uint8_t>, std::string> get_data(uint32_t row, std::string_view col_name) const {
        int col = find_column(col_name);
        if (col < 0) return std::unexpected("UTF column not found: " + std::string(col_name));
        return get_data(row, static_cast<uint32_t>(col));
    }
    std::expected<std::span<const uint8_t>, std::string> get_default_data(uint32_t col) const;
    std::expected<std::span<const uint8_t>, std::string> get_default_data(std::string_view col_name) const {
        int col = find_column(col_name);
        if (col < 0) return std::unexpected("UTF column not found: " + std::string(col_name));
        return get_default_data(static_cast<uint32_t>(col));
    }
    
    std::expected<std::string_view, std::string> get_string(uint32_t row, uint32_t col) const;
    std::expected<std::string_view, std::string> get_string(uint32_t row, std::string_view col_name) const {
        int col = find_column(col_name);
        if (col < 0) return std::unexpected("UTF column not found: " + std::string(col_name));
        return get_string(row, static_cast<uint32_t>(col));
    }
    std::expected<std::string_view, std::string> get_string_bytes(uint32_t row, uint32_t col) const {
        return get_string(row, col);
    }
    std::expected<std::string_view, std::string> get_string_bytes(uint32_t row, std::string_view col_name) const {
        return get_string(row, col_name);
    }
    
    static UtfTable create(std::string_view name, uint16_t version = 0);
    void add_column(std::string_view name, ColumnType type, ColumnFlag flag = ColumnFlag::Name);
    uint32_t add_row();
    bool remove_row(uint32_t row);
    bool move_row(uint32_t from_row, uint32_t to_row);
    bool remove_column(uint32_t col);
    bool rename_column(uint32_t col, std::string_view name);
    bool set_column_type(uint32_t col, ColumnType type);
    bool set_column_flag(uint32_t col, ColumnFlag flag);
    [[nodiscard]] UtfTable editable_copy() const;
    
    std::expected<void, std::string> set(uint32_t row, uint32_t col, Value value);
    std::expected<void, std::string> set(uint32_t row, std::string_view col_name, Value value) {
        int col = find_column(col_name);
        if (col < 0) {
            return std::unexpected("UTF set failed: column not found: " + std::string(col_name));
        }
        return set(row, static_cast<uint32_t>(col), std::move(value));
    }
    std::expected<void, std::string> set_default_value(uint32_t col, Value value);
    std::expected<void, std::string> set_default_value(std::string_view col_name, Value value) {
        int col = find_column(col_name);
        if (col < 0) {
            return std::unexpected("UTF default set failed: column not found: " + std::string(col_name));
        }
        return set_default_value(static_cast<uint32_t>(col), std::move(value));
    }
    
    std::vector<uint8_t> build() const;
    
    bool is_loaded() const { return !m_source.empty(); }
    
private:
    uint32_t m_table_size = 0;
    uint16_t m_version = 0;
    uint32_t m_rows_offset = 0;
    uint32_t m_strings_offset = 0;
    uint32_t m_data_offset = 0;
    uint16_t m_row_width = 0;
    uint32_t m_loaded_row_count = 0;
    uint32_t m_data_alignment = 0;

    io::SourceView m_source;
    
    std::vector<Column> m_columns;
    std::string m_table_name;
    std::optional<std::string> m_text_encoding;

    std::vector<std::vector<Value>> m_values;
    std::vector<Value> m_default_values;
    
    mutable util::flat_unordered_map<std::string, int, util::transparent_string_hash, std::equal_to<>> m_column_cache;
    
    std::expected<void, std::string> parse();
    void make_editable();
    
    std::expected<std::span<const uint8_t>, std::string> field_data(uint32_t row, uint32_t col) const;
    std::expected<std::span<const uint8_t>, std::string> data_at(DataRef ref) const;

    Value read_value_at(const uint8_t* buf, ColumnType type) const;
    std::string_view string_at(uint32_t offset) const;
};

} // namespace cricodecs::utf
