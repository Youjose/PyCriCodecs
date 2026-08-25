#pragma once
/**
 * @file csb_format.hpp
 * @brief Internal CSB UTF table names, format tags, and wrapper helpers.
 *
 * These helpers follow the current CSB model and wrapper handling
 * recovered during the CriCodecs C++23 port. Implementation by Youjose.
 */

#include "csb_container.hpp"

#include <expected>
#include <span>
#include <string>
#include <string_view>

#include "../utilities/text_encoding.hpp"

namespace cricodecs::csb::detail {

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

inline std::expected<std::string, std::string> require_string(
    const utf::UtfTable& table,
    uint32_t row,
    std::string_view column
) {
    auto value = table.get_string(row, column);
    if (!value) {
        return std::unexpected(
            "Missing or invalid '" + std::string(column) + "' at row " +
            std::to_string(row) + ": " + value.error());
    }
    return std::string(*value);
}

inline std::expected<std::string, std::string> decode_cri_string(
    std::string_view raw,
    const text::EncodingOptions& encoding,
    std::string_view context
) {
    auto decoded = text::decode_to_utf8(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()),
        encoding
    );
    if (!decoded) {
        return std::unexpected(std::string(context) + ": " + decoded.error());
    }
    return *decoded;
}

inline std::expected<std::string, std::string> encode_cri_string_to_storage(
    std::string_view text_value,
    const text::EncodingOptions& encoding,
    std::string_view context
) {
    auto encoded = text::encode_cri_string(text_value, encoding);
    if (!encoded) {
        return std::unexpected(std::string(context) + ": " + encoded.error());
    }
    return std::string(reinterpret_cast<const char*>(encoded->data()), encoded->size());
}

inline std::expected<std::span<const uint8_t>, std::string> require_data(
    const utf::UtfTable& table,
    uint32_t row,
    std::string_view column,
    std::string_view context
) {
    auto value = table.get_data(row, column);
    if (!value) {
        return std::unexpected(
            std::string(context) + ": missing or invalid '" + std::string(column) +
            "' at row " + std::to_string(row) + ": " + value.error());
    }
    return *value;
}

[[nodiscard]] inline const char* expected_wrapper_table_name(uint8_t format) noexcept {
    switch (format) {
        case 0: return "AAX";
        case 2: return "AHX";
        case 4: return "ADPCM_WII";
        case 6: return "HCA";
        default: return nullptr;
    }
}

[[nodiscard]] inline std::expected<utf::UtfTable, std::string> parse_wrapper_table(
    std::span<const uint8_t> wrapper
) {
    auto table = utf::UtfTable::load(wrapper);
    if (!table) {
        return std::unexpected("CSB parse failed: could not parse wrapped UTF payload: " + table.error());
    }

    return std::move(*table);
}

[[nodiscard]] inline std::expected<std::string, std::string> parse_wrapper_table_name(
    std::span<const uint8_t> wrapper
) {
    return parse_wrapper_table(wrapper).transform([](const auto& table) {
        return std::string(table.table_name());
    });
}

[[nodiscard]] inline std::expected<uint8_t, std::string> read_segment_loop_flag(
    const utf::UtfTable& wrapper,
    uint32_t row
) {
    auto value = wrapper.get<uint8_t>(row, "lpflg");
    if (!value) {
        return std::unexpected(
            "CSB parse failed: missing or invalid 'lpflg' at row " +
            std::to_string(row) + ": " + value.error());
    }
    return *value;
}

[[nodiscard]] inline std::expected<void, std::string> validate_wrapper_table_name(
    uint8_t format,
    std::string_view wrapper_table_name
) {
    const char* expected_name = expected_wrapper_table_name(format);
    if (expected_name == nullptr) {
        return {};
    }

    if (wrapper_table_name != expected_name) {
        return std::unexpected(
            "Unexpected wrapper table name for fmt " + std::to_string(format) +
            ": expected " + expected_name + ", got " + std::string(wrapper_table_name));
    }

    return {};
}

} // namespace cricodecs::csb::detail
