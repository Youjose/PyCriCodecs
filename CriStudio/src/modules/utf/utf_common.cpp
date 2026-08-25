#include "shared/i18n.hpp"
#include "modules/utf/utf_common.hpp"

#include "shared/document_helpers.hpp"

#include <array>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>
#include <variant>

namespace cristudio::modules::utf {
namespace {

bool starts_with(std::span<const uint8_t> bytes, std::string_view magic) {
    if (bytes.size() < magic.size()) {
        return false;
    }
    for (size_t i = 0; i < magic.size(); ++i) {
        if (bytes[i] != static_cast<uint8_t>(magic[i])) {
            return false;
        }
    }
    return true;
}

std::string embedded_format_probe(std::span<const uint8_t> bytes) {
    if (starts_with(bytes, "@UTF")) return "UTF";
    if (starts_with(bytes, "CPK ")) return "CPK";
    if (starts_with(bytes, "CRID")) return "USM";
    if (starts_with(bytes, "AFS\0")) return "AFS";
    if (starts_with(bytes, "AFS2")) return "AWB";
    if (starts_with(bytes, "HCA\0")) return "HCA";
    if (starts_with(bytes, "RIFF")) return "WAV";
    if (bytes.size() >= 2 && bytes[0] == 0x80) return "ADX";
    return {};
}

} // namespace

std::string column_type_name(cricodecs::utf::ColumnType type) {
    using cricodecs::utf::ColumnType;
    switch (type) {
    case ColumnType::UInt8: return "u8";
    case ColumnType::SInt8: return "s8";
    case ColumnType::UInt16: return "u16";
    case ColumnType::SInt16: return "s16";
    case ColumnType::UInt32: return "u32";
    case ColumnType::SInt32: return "s32";
    case ColumnType::UInt64: return "u64";
    case ColumnType::SInt64: return "s64";
    case ColumnType::Float: return "float";
    case ColumnType::Double: return "double";
    case ColumnType::String: return "string";
    case ColumnType::VLData: return "binary";
    case ColumnType::GUID: return "guid";
    }
    return "unknown";
}

std::string column_flag_text(cricodecs::utf::ColumnFlag flags) {
    std::vector<std::string> parts;
    if (cricodecs::utf::has_flag(flags, cricodecs::utf::ColumnFlag::Name)) {
        parts.push_back("name");
    }
    if (cricodecs::utf::has_flag(flags, cricodecs::utf::ColumnFlag::Default)) {
        parts.push_back("default");
    }
    if (cricodecs::utf::has_flag(flags, cricodecs::utf::ColumnFlag::Row)) {
        parts.push_back("row");
    }
    if (parts.empty()) {
        return "none";
    }

    std::ostringstream out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            out << '|';
        }
        out << parts[i];
    }
    return out.str();
}

std::string guid_text(const cricodecs::utf::GUID& guid) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (size_t i = 0; i < std::size(guid.data); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out << '-';
        }
        out << std::setw(2) << static_cast<unsigned>(guid.data[i]);
    }
    return out.str();
}

std::string data_probe_text(std::span<const uint8_t> bytes) {
    if (bytes.empty()) {
        return "empty";
    }
    if (auto format = embedded_format_probe(bytes); !format.empty()) {
        return cristudio::i18n::translate_utf8("Utf.UtfCommon", "looks like ") + format;
    }
    return cristudio::i18n::translate_utf8("Utf.UtfCommon", "binary data");
}

std::string value_text(const cricodecs::utf::UtfTable& utf, uint32_t row, uint32_t col) {
    auto value = utf.get_value(row, col);
    if (!value) {
        return value.error();
    }

    return std::visit([&utf, row, col](const auto& item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return "<none>";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return item.empty() ? "\"\"" : item;
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            return byte_count(item.size());
        } else if constexpr (std::is_same_v<T, cricodecs::utf::DataRef>) {
            auto data = utf.get_data(row, col);
            const auto suffix = data ? ", " + data_probe_text(*data) : std::string{};
            return byte_count(item.size) + " at " + hex_number(item.offset) + suffix;
        } else if constexpr (std::is_same_v<T, cricodecs::utf::GUID>) {
            return guid_text(item);
        } else if constexpr (std::is_floating_point_v<T>) {
            std::ostringstream out;
            out << item;
            return out.str();
        } else if constexpr (std::is_integral_v<T>) {
            const auto unsigned_value = static_cast<uint64_t>(item);
            if constexpr (std::is_signed_v<T>) {
                return std::to_string(static_cast<int64_t>(item)) + " (" + hex_number(unsigned_value) + ")";
            } else {
                return std::to_string(unsigned_value) + " (" + hex_number(unsigned_value) + ")";
            }
        } else {
            return {};
        }
    }, *value);
}

std::string row_label(const cricodecs::utf::UtfTable& utf, uint32_t row) {
    static constexpr std::array<std::string_view, 8> preferred_names = {
        "FileName", "filename", "CueName", "Name", "DirName", "TableName", "Type", "ID"
    };

    for (const auto name : preferred_names) {
        const auto col = utf.find_column(name);
        if (col < 0) {
            continue;
        }
        if (auto text = utf.get_string(row, static_cast<uint32_t>(col)); text && !text->empty()) {
            return "row " + number(row) + " - " + std::string(*text);
        }
        if (auto value = utf.get_value(row, static_cast<uint32_t>(col)); value && !std::holds_alternative<std::monostate>(*value)) {
            return "row " + number(row) + " - " + value_text(utf, row, static_cast<uint32_t>(col));
        }
    }

    return "row " + number(row);
}

std::optional<uint32_t> source_index(uint32_t row, uint32_t column_count, uint32_t col) {
    const auto index = static_cast<uint64_t>(row) * column_count + col;
    if (index > std::numeric_limits<uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(index);
}

std::optional<std::pair<uint32_t, uint32_t>> row_col_from_source_index(
    const cricodecs::utf::UtfTable& utf,
    uint32_t source_index
) {
    const auto columns = utf.column_count();
    if (columns == 0) {
        return std::nullopt;
    }
    const auto row = source_index / columns;
    const auto col = source_index % columns;
    if (row >= utf.row_count() || col >= columns) {
        return std::nullopt;
    }
    return std::pair{row, col};
}

std::expected<std::vector<uint8_t>, std::string> extract_cell_data(
    const cricodecs::utf::UtfTable& utf,
    uint32_t source_index
) {
    const auto row_col = row_col_from_source_index(utf, source_index);
    if (!row_col) {
        return std::unexpected(cristudio::i18n::translate_utf8("Utf.UtfCommon", "UTF source index is out of range"));
    }

    const auto [row, col] = *row_col;
    auto data = utf.get_data(row, col);
    if (!data) {
        return std::unexpected(data.error());
    }
    return std::vector<uint8_t>(data->begin(), data->end());
}

} // namespace cristudio::modules::utf
