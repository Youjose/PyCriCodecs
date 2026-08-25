#pragma once

#include "document/document_types.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace cristudio {

[[nodiscard]] std::string generic_path(const std::filesystem::path& path);
[[nodiscard]] std::string filename_of(const std::filesystem::path& path);
[[nodiscard]] std::string display_path_separators(std::string text);
[[nodiscard]] std::string archive_display_path(std::string_view text);
[[nodiscard]] std::string archive_leaf_name(std::string_view text);
[[nodiscard]] std::string lower_extension_text(std::string_view name);
[[nodiscard]] std::string bool_text(bool value);
[[nodiscard]] std::string number(uint64_t value);
[[nodiscard]] std::string hex_number(uint64_t value);
[[nodiscard]] std::string byte_count(uint64_t value);
[[nodiscard]] std::string indexed_label(std::string_view label, uint64_t value);
[[nodiscard]] std::string float_text(float value);
[[nodiscard]] InfoRow translated_info_row(
    const char* context,
    const char* source,
    std::string value,
    std::string id = {},
    std::string value_id = {});
[[nodiscard]] InfoRow translated_info_row_with_value(
    const char* name_context,
    const char* name_source,
    const char* value_context,
    const char* value_source,
    std::string id = {},
    std::string value_id = {});
[[nodiscard]] InfoRow translated_info_row_with_affix(
    const char* context,
    const char* source,
    std::string affix,
    InfoRow::TranslationAffix affix_position,
    std::string value,
    std::string id = {},
    std::string value_id = {});
[[nodiscard]] std::string translated_info_name(const InfoRow& row);
[[nodiscard]] std::string translated_info_value(const InfoRow& row);

void add_source_info(LoadedDocument& doc);
[[nodiscard]] LoadedDocument base_document(const std::filesystem::path& path, std::string format);
[[nodiscard]] EntrySummary source_entry(
    EntrySummary entry,
    const std::filesystem::path& source_path,
    std::string source_format,
    uint32_t source_index);

} // namespace cristudio
