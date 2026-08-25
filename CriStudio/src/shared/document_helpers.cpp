#include "shared/i18n.hpp"
#include "shared/document_helpers.hpp"

#include "path_text.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace cristudio {

std::string generic_path(const std::filesystem::path& path) {
    return path_to_utf8(path);
}

std::string filename_of(const std::filesystem::path& path) {
    auto name = filename_to_utf8(path);
    return name.empty() ? generic_path(path) : name;
}

std::string display_path_separators(std::string text) {
    std::ranges::replace(text, '\\', '/');
    return text;
}

std::string archive_display_path(std::string_view text) {
    return display_path_separators(std::string(text));
}

std::string archive_leaf_name(std::string_view text) {
    auto path = archive_display_path(text);
    while (!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    const auto slash = path.find_last_of('/');
    if (slash != std::string::npos && slash + 1 < path.size()) {
        return path.substr(slash + 1);
    }
    return path;
}

std::string lower_extension_text(std::string_view name) {
    auto normalized = archive_display_path(name);
    const auto slash = normalized.find_last_of('/');
    const auto dot = normalized.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return {};
    }
    auto ext = normalized.substr(dot);
    std::ranges::transform(ext, ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext;
}

std::string bool_text(bool value) {
    return value ? "yes" : "no";
}

std::string number(uint64_t value) {
    return std::to_string(value);
}

std::string hex_number(uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << value;
    return out.str();
}

std::string byte_count(uint64_t value) {
    std::ostringstream out;
    out << value << " bytes";
    return out.str();
}

std::string indexed_label(std::string_view label, uint64_t value) {
    std::string text(label);
    text.push_back(' ');
    text += number(value);
    return text;
}

std::string float_text(float value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

InfoRow translated_info_row(
    const char* context,
    const char* source,
    std::string value,
    std::string id,
    std::string value_id) {
    return {
        cristudio::i18n::translate_utf8(context, source),
        std::move(value),
        id.empty() ? std::string(source) : std::move(id),
        std::move(value_id),
        context,
        source,
    };
}

InfoRow translated_info_row_with_value(
    const char* name_context,
    const char* name_source,
    const char* value_context,
    const char* value_source,
    std::string id,
    std::string value_id) {
    auto row = translated_info_row(
        name_context,
        name_source,
        cristudio::i18n::translate_utf8(value_context, value_source),
        std::move(id),
        std::move(value_id));
    row.value_translation_context = value_context;
    row.value_translation_source = value_source;
    return row;
}

InfoRow translated_info_row_with_affix(
    const char* context,
    const char* source,
    std::string affix,
    InfoRow::TranslationAffix affix_position,
    std::string value,
    std::string id,
    std::string value_id) {
    auto row = translated_info_row(
        context,
        source,
        std::move(value),
        std::move(id),
        std::move(value_id));
    row.name_translation_affix = std::move(affix);
    row.name_translation_affix_position = affix_position;
    row.name = translated_info_name(row);
    return row;
}

std::string translated_info_name(const InfoRow& row) {
    std::string translated;
    if (row.name_translation_context != nullptr && row.name_translation_source != nullptr) {
        translated = cristudio::i18n::translate_utf8(
            row.name_translation_context,
            row.name_translation_source);
    } else {
        translated = row.name;
    }
    if (row.name_translation_affix_position == InfoRow::TranslationAffix::Prefix) {
        return row.name_translation_affix + translated;
    }
    if (row.name_translation_affix_position == InfoRow::TranslationAffix::Suffix) {
        translated += row.name_translation_affix;
    }
    return translated;
}

std::string translated_info_value(const InfoRow& row) {
    if (row.value_translation_context != nullptr && row.value_translation_source != nullptr) {
        return cristudio::i18n::translate_utf8(row.value_translation_context, row.value_translation_source);
    }
    return row.value;
}

void add_source_info(LoadedDocument& doc) {
    doc.info.push_back(translated_info_row("Shared.DocumentHelpers", "Path", generic_path(doc.path)));
    doc.info.push_back(translated_info_row("Shared.DocumentHelpers", "Size", byte_count(doc.file_size)));
}

LoadedDocument base_document(const std::filesystem::path& path, std::string format) {
    LoadedDocument doc;
    doc.path = path;
    doc.display_name = filename_of(path);
    doc.format = std::move(format);
    std::error_code ec;
    doc.file_size = std::filesystem::file_size(path, ec);
    add_source_info(doc);
    return doc;
}

EntrySummary source_entry(
    EntrySummary entry,
    const std::filesystem::path& source_path,
    std::string source_format,
    uint32_t source_index) {
    entry.source_path = source_path;
    entry.source_format = std::move(source_format);
    entry.source_index = source_index;
    entry.has_source = true;
    return entry;
}

} // namespace cristudio
