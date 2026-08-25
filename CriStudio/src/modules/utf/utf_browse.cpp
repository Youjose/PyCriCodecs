#include "shared/i18n.hpp"
#include "modules/utf/utf_browse.hpp"

#include "modules/utf/utf_common.hpp"
#include "shared/document_helpers.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <string_view>
#include <variant>

namespace cristudio::modules::utf {
namespace {

std::string column_key(std::string_view name) {
    std::string key;
    key.reserve(name.size());
    for (const auto ch : name) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return key;
}

bool column_key_contains(std::string_view key, std::string_view needle) {
    return key.find(needle) != std::string_view::npos;
}

std::vector<uint32_t> choose_visible_columns(const cricodecs::utf::UtfTable& utf) {
    static constexpr uint32_t max_visible_columns = 4;
    static constexpr std::array<std::string_view, 23> exact_priority = {
        "filename",
        "pathname",
        "path",
        "dirname",
        "name",
        "cuename",
        "waveformname",
        "id",
        "cueid",
        "waveformid",
        "filesize",
        "datasize",
        "extractsize",
        "fileoffset",
        "offset",
        "size",
        "chno",
        "stmid",
        "streamid",
        "avbps",
        "fmtver",
        "minbuf",
        "minchk"
    };

    const auto column_count = utf.column_count();
    std::vector<std::string> keys;
    keys.reserve(column_count);
    for (uint32_t col = 0; col < column_count; ++col) {
        keys.push_back(column_key(utf.column(col).name));
    }

    std::vector<uint32_t> selected;
    selected.reserve(std::min(column_count, max_visible_columns));
    std::vector<bool> used(column_count, false);
    const auto add_column = [&](uint32_t col) {
        if (col >= column_count || used[col] || selected.size() >= max_visible_columns) {
            return;
        }
        used[col] = true;
        selected.push_back(col);
    };

    for (const auto wanted : exact_priority) {
        for (uint32_t col = 0; col < column_count; ++col) {
            if (keys[col] == wanted) {
                add_column(col);
            }
        }
    }

    for (uint32_t col = 0; col < column_count && selected.size() < max_visible_columns; ++col) {
        const auto& key = keys[col];
        if (
            column_key_contains(key, "name") ||
            column_key_contains(key, "path") ||
            column_key_contains(key, "size") ||
            column_key_contains(key, "offset") ||
            column_key_contains(key, "id")
        ) {
            add_column(col);
        }
    }

    for (uint32_t col = 0; col < column_count && selected.size() < max_visible_columns; ++col) {
        add_column(col);
    }

    return selected;
}

} // namespace

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::utf::UtfTable& utf) {
    auto doc = base_document(path, cristudio::i18n::translate_utf8("Utf.UtfBrowse", "UTF table"));
    doc.info.push_back(translated_info_row("Utf.UtfBrowse", "Table", std::string(utf.table_name())));
    doc.info.push_back(translated_info_row("Utf.UtfBrowse", "Version", number(utf.version())));
    doc.info.push_back(translated_info_row("Utf.UtfBrowse", "Rows", number(utf.row_count())));
    doc.info.push_back(translated_info_row("Utf.UtfBrowse", "Columns", number(utf.columns().size())));
    doc.info.push_back(translated_info_row("Utf.UtfBrowse", "Row width", byte_count(utf.row_width())));
    doc.info.push_back(translated_info_row("Utf.UtfBrowse", "Table size", byte_count(utf.table_size())));

    const auto column_count = utf.column_count();
    if (utf.row_count() == 0) {
        doc.entry_columns = {"Column", "Type", "Flags"};
        doc.entry_column_types = {"schema", "type", "flags"};
        doc.entries.reserve(utf.columns().size());
        for (const auto& column : utf.columns()) {
            EntrySummary entry;
            entry.name = column.name;
            entry.type = cristudio::i18n::translate_utf8("Utf.UtfBrowse", "column");
            entry.detail = column_flag_text(column.flag);
            entry.cells = {column.name, column_type_name(column.type), entry.detail};
            doc.entries.push_back(std::move(entry));
        }
        return doc;
    }

    const auto visible_columns = choose_visible_columns(utf);
    doc.entry_columns.reserve(visible_columns.size() + 1);
    doc.entry_column_types.reserve(visible_columns.size() + 1);
    doc.entry_columns.push_back("Row");
    doc.entry_column_types.push_back(cristudio::i18n::translate_utf8("Utf.UtfBrowse", "row"));
    for (const auto col : visible_columns) {
        const auto& column = utf.column(col);
        doc.entry_columns.push_back(column.name);
        doc.entry_column_types.push_back(column_type_name(column.type));
    }

    doc.entries.reserve(utf.row_count());
    for (uint32_t row = 0; row < utf.row_count(); ++row) {
        const auto label = row_label(utf, row);
        EntrySummary entry;
        entry.name = label;
        entry.type = cristudio::i18n::translate_utf8("Utf.UtfBrowse", "row");
        entry.detail = number(column_count) + (column_count == 1 ? cristudio::i18n::translate_utf8("Utf.UtfBrowse", " field") : cristudio::i18n::translate_utf8("Utf.UtfBrowse", " fields"));
        entry.source_path = path;
        entry.source_format = "UTF";
        entry.cells.reserve(visible_columns.size() + 1);
        entry.cell_source_indices.assign(
            visible_columns.size() + 1,
            std::numeric_limits<uint32_t>::max()
        );
        entry.cells.push_back(label);
        entry.inspector_entries.reserve(column_count);
        std::vector<std::string> row_cells(column_count);
        std::vector<uint32_t> row_source_indices(column_count, std::numeric_limits<uint32_t>::max());
        for (uint32_t col = 0; col < column_count; ++col) {
            const auto& column = utf.column(col);
            const auto type = column_type_name(column.type);
            auto value = utf.get_value(row, col);
            auto cell_text = value ? value_text(utf, row, col) : value.error();
            EntrySummary field;
            field.name = column.name;
            field.type = type;
            field.detail = cell_text;
            field.source_path = path;
            field.source_format = "UTF";
            field.cells = {column.name, type, cell_text};

            if (column.type == cricodecs::utf::ColumnType::VLData) {
                if (auto data = utf.get_data(row, col)) {
                    cell_text = byte_count(data->size()) + ", " + data_probe_text(*data);
                    field.detail = cell_text;
                    field.size = byte_count(data->size());
                    field.cells = {column.name, type, cell_text};
                    if (auto index = source_index(row, column_count, col)) {
                        field.source_index = *index;
                        field.has_source = true;
                        entry.has_cell_sources = true;
                        row_source_indices[col] = *index;
                    }
                } else if (!value) {
                    cell_text = value.error();
                    field.detail = cell_text;
                    field.cells = {column.name, type, cell_text};
                }
                if (value && std::holds_alternative<cricodecs::utf::DataRef>(*value)) {
                    const auto ref = std::get<cricodecs::utf::DataRef>(*value);
                    field.offset = hex_number(static_cast<uint64_t>(utf.data_offset()) + ref.offset);
                }
            }

            row_cells[col] = cell_text;
            entry.inspector_entries.push_back(std::move(field));
        }
        for (size_t visible = 0; visible < visible_columns.size(); ++visible) {
            const auto col = visible_columns[visible];
            entry.cells.push_back(row_cells[col]);
            entry.cell_source_indices[visible + 1] = row_source_indices[col];
        }
        doc.entries.push_back(std::move(entry));
    }
    return doc;
}

} // namespace cristudio::modules::utf
