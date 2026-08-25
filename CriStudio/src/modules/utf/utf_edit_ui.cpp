#include "modules/utf/utf_edit_ui.hpp"

#include "editor/editor_helpers.hpp"
#include "modules/utf/utf_edit.hpp"
#include "path_text.hpp"

#include <QCoreApplication>
#include <QLineEdit>
#include <QHeaderView>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <algorithm>
#include <limits>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

namespace cristudio::modules::utf {
namespace {

} // namespace

QString utf_type_name(cricodecs::utf::ColumnType type) {
    using cricodecs::utf::ColumnType;
    switch (type) {
    case ColumnType::UInt8: return QStringLiteral("u8");
    case ColumnType::SInt8: return QStringLiteral("s8");
    case ColumnType::UInt16: return QStringLiteral("u16");
    case ColumnType::SInt16: return QStringLiteral("s16");
    case ColumnType::UInt32: return QStringLiteral("u32");
    case ColumnType::SInt32: return QStringLiteral("s32");
    case ColumnType::UInt64: return QStringLiteral("u64");
    case ColumnType::SInt64: return QStringLiteral("s64");
    case ColumnType::Float: return QStringLiteral("float");
    case ColumnType::Double: return QStringLiteral("double");
    case ColumnType::String: return QStringLiteral("string");
    case ColumnType::VLData: return QStringLiteral("binary");
    case ColumnType::GUID: return QStringLiteral("guid");
    }
    return QStringLiteral("unknown");
}

QString utf_flag_name(cricodecs::utf::ColumnFlag flag) {
    QStringList parts{QStringLiteral("name")};
    if (cricodecs::utf::has_flag(flag, cricodecs::utf::ColumnFlag::Default)) {
        parts.push_back(QStringLiteral("default"));
    }
    if (cricodecs::utf::has_flag(flag, cricodecs::utf::ColumnFlag::Row)) {
        parts.push_back(QStringLiteral("row"));
    }
    return parts.join(QLatin1Char('|'));
}

std::optional<cricodecs::utf::ColumnType> utf_type_from_name(QString text) {
    text = text.trimmed().toLower();
    using cricodecs::utf::ColumnType;
    if (text == QStringLiteral("u8") || text == QStringLiteral("uint8")) return ColumnType::UInt8;
    if (text == QStringLiteral("s8") || text == QStringLiteral("int8")) return ColumnType::SInt8;
    if (text == QStringLiteral("u16") || text == QStringLiteral("uint16")) return ColumnType::UInt16;
    if (text == QStringLiteral("s16") || text == QStringLiteral("int16")) return ColumnType::SInt16;
    if (text == QStringLiteral("u32") || text == QStringLiteral("uint32")) return ColumnType::UInt32;
    if (text == QStringLiteral("s32") || text == QStringLiteral("int32")) return ColumnType::SInt32;
    if (text == QStringLiteral("u64") || text == QStringLiteral("uint64")) return ColumnType::UInt64;
    if (text == QStringLiteral("s64") || text == QStringLiteral("int64")) return ColumnType::SInt64;
    if (text == QStringLiteral("float")) return ColumnType::Float;
    if (text == QStringLiteral("double")) return ColumnType::Double;
    if (text == QStringLiteral("string")) return ColumnType::String;
    if (text == QStringLiteral("binary") || text == QStringLiteral("vldata")) return ColumnType::VLData;
    if (text == QStringLiteral("guid")) return ColumnType::GUID;
    return std::nullopt;
}

std::optional<cricodecs::utf::ColumnFlag> utf_flag_from_name(QString text) {
    text = text.trimmed().toLower();
    text.replace(QLatin1Char('+'), QLatin1Char('|'));
    const auto parts = text.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    auto flag = cricodecs::utf::ColumnFlag::Name;
    bool saw_name = false;
    for (auto part : parts) {
        part = part.trimmed();
        if (part == QStringLiteral("name")) {
            saw_name = true;
        } else if (part == QStringLiteral("default")) {
            flag = flag | cricodecs::utf::ColumnFlag::Default;
        } else if (part == QStringLiteral("row")) {
            flag = flag | cricodecs::utf::ColumnFlag::Row;
        } else {
            return std::nullopt;
        }
    }
    return saw_name || parts.empty() ? std::optional(flag) : std::optional(flag);
}

cricodecs::utf::Value utf_value_for_cell(
    const cricodecs::utf::UtfTable& utf,
    uint32_t row,
    uint32_t col
) {
    if (col >= utf.column_count() || row >= utf.row_count()) {
        return std::monostate{};
    }
    if (utf.column(col).type == cricodecs::utf::ColumnType::VLData) {
        if (auto data = utf.get_data(row, col)) {
            return std::vector<uint8_t>(data->begin(), data->end());
        }
        return std::monostate{};
    }
    if (auto value = utf.get_value(row, col)) {
        return *value;
    }
    return std::monostate{};
}

cricodecs::utf::Value utf_default_for_column(const cricodecs::utf::UtfTable& utf, uint32_t col) {
    if (col >= utf.column_count()) {
        return std::monostate{};
    }
    if (!cricodecs::utf::has_flag(utf.column(col).flag, cricodecs::utf::ColumnFlag::Default)) {
        return std::monostate{};
    }
    if (utf.column(col).type == cricodecs::utf::ColumnType::VLData) {
        if (auto data = utf.get_default_data(col)) {
            return std::vector<uint8_t>(data->begin(), data->end());
        }
        return std::monostate{};
    }
    if (auto value = utf.get_default_value(col)) {
        return *value;
    }
    return std::monostate{};
}

QString utf_value_text(const cricodecs::utf::Value& value) {
    using namespace cricodecs::utf;
    return std::visit([](const auto& item) -> QString {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return QStringLiteral("<none>");
        } else if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t> || std::is_same_v<T, uint32_t>) {
            return QString::number(item);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return QString::number(static_cast<qulonglong>(item));
        } else if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, int16_t> || std::is_same_v<T, int32_t>) {
            return QString::number(item);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return QString::number(static_cast<qlonglong>(item));
        } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
            return QString::number(item, 'g', 9);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return utf8_to_qstring(item);
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            return QCoreApplication::translate("Utf.UtfEditUi", "%1 bytes").arg(static_cast<qulonglong>(item.size()));
        } else if constexpr (std::is_same_v<T, DataRef>) {
            return QCoreApplication::translate("Utf.UtfEditUi", "offset 0x%1, %2 bytes")
                .arg(item.offset, 0, 16)
                .arg(item.size);
        } else if constexpr (std::is_same_v<T, GUID>) {
            QString out;
            out.reserve(32);
            for (const auto byte : item.data) {
                out += QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0'));
            }
            return out.toUpper();
        }
        return QStringLiteral("<unsupported>");
    }, value);
}

QString utf_editable_value_text(const cricodecs::utf::Value& value) {
    using namespace cricodecs::utf;
    return std::visit([](const auto& item) -> QString {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            return bytes_to_hex(item);
        } else if constexpr (std::is_same_v<T, GUID>) {
            return bytes_to_hex(std::span<const uint8_t>(item.data, sizeof(item.data)));
        } else {
            return utf_value_text(item);
        }
    }, value);
}

void populate_utf_tables(const cricodecs::utf::UtfTable& utf, UtfTableWidgets widgets) {
    if (widgets.table_name != nullptr) {
        widgets.table_name->setText(utf8_to_qstring(utf.table_name()));
    }

    if (widgets.grid != nullptr) {
        widgets.grid->clear();
        const bool transpose = widgets.transpose_single_row && utf.row_count() == 1;
        if (transpose) {
            widgets.grid->setRowCount(static_cast<int>(utf.column_count()));
            widgets.grid->setColumnCount(3);
            widgets.grid->setHorizontalHeaderLabels({
                QCoreApplication::translate("Utf.UtfEditUi", "Field"), QCoreApplication::translate("Utf.UtfEditUi", "Type"), QCoreApplication::translate("Utf.UtfEditUi", "Value")
            });
            for (uint32_t col = 0; col < utf.column_count(); ++col) {
                const auto& column = utf.column(col);
                auto* name_item = new QTableWidgetItem(utf8_to_qstring(column.name));
                name_item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                auto* type_item = new QTableWidgetItem(utf_type_name(column.type));
                type_item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                auto* value_item = new QTableWidgetItem(utf_value_text(utf_value_for_cell(utf, 0, col)));
                value_item->setData(Qt::UserRole, static_cast<int>(col));
                if (column.type == cricodecs::utf::ColumnType::VLData) {
                    value_item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                }
                widgets.grid->setItem(static_cast<int>(col), 0, name_item);
                widgets.grid->setItem(static_cast<int>(col), 1, type_item);
                widgets.grid->setItem(static_cast<int>(col), 2, value_item);
            }
            widgets.grid->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            widgets.grid->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
            widgets.grid->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        } else {
            widgets.grid->setRowCount(static_cast<int>(utf.row_count()));
            widgets.grid->setColumnCount(static_cast<int>(utf.column_count()));
        QStringList headers;
        for (uint32_t col = 0; col < utf.column_count(); ++col) {
            const auto& column = utf.column(col);
            headers.push_back(utf8_to_qstring(column.name) + QStringLiteral("\n") + utf_type_name(column.type));
        }
        widgets.grid->setHorizontalHeaderLabels(headers);

        for (uint32_t row = 0; row < utf.row_count(); ++row) {
            for (uint32_t col = 0; col < utf.column_count(); ++col) {
                auto* item = new QTableWidgetItem(utf_value_text(utf_value_for_cell(utf, row, col)));
                item->setData(Qt::UserRole, static_cast<int>(col));
                widgets.grid->setItem(static_cast<int>(row), static_cast<int>(col), item);
            }
        }

        for (int col = 0; col < widgets.grid->columnCount(); ++col) {
            widgets.grid->resizeColumnToContents(col);
        }
        }
    }

    if (widgets.schema != nullptr) {
        widgets.schema->setRowCount(static_cast<int>(utf.column_count()));
        for (uint32_t col = 0; col < utf.column_count(); ++col) {
            const auto& column = utf.column(col);
            widgets.schema->setItem(static_cast<int>(col), 0, new QTableWidgetItem(utf8_to_qstring(column.name)));
            widgets.schema->setItem(static_cast<int>(col), 1, new QTableWidgetItem(utf_type_name(column.type)));
            widgets.schema->setItem(static_cast<int>(col), 2, new QTableWidgetItem(utf_flag_name(column.flag)));
            widgets.schema->setItem(static_cast<int>(col), 3, new QTableWidgetItem(utf_editable_value_text(utf_default_for_column(utf, col))));
            auto* default_offset_item = new QTableWidgetItem(QStringLiteral("0x%1").arg(column.default_offset, 0, 16).toUpper());
            default_offset_item->setFlags(default_offset_item->flags() & ~Qt::ItemIsEditable);
            widgets.schema->setItem(static_cast<int>(col), 4, default_offset_item);
            auto* row_offset_item = new QTableWidgetItem(QStringLiteral("0x%1").arg(column.row_offset, 0, 16).toUpper());
            row_offset_item->setFlags(row_offset_item->flags() & ~Qt::ItemIsEditable);
            widgets.schema->setItem(static_cast<int>(col), 5, row_offset_item);
            auto* index_item = new QTableWidgetItem(QString::number(col));
            index_item->setFlags(index_item->flags() & ~Qt::ItemIsEditable);
            widgets.schema->setItem(static_cast<int>(col), 6, index_item);
        }

        for (int col = 0; col < widgets.schema->columnCount(); ++col) {
            widgets.schema->resizeColumnToContents(col);
        }
    }
}

QString utf_table_preview(const cricodecs::utf::UtfTable& table, size_t max_rows, int max_value_chars) {
    QStringList lines;
    lines.push_back(QCoreApplication::translate("Utf.UtfEditUi", "Table: %1").arg(utf8_to_qstring(std::string(table.table_name()))));
    lines.push_back(QCoreApplication::translate("Utf.UtfEditUi", "Rows: %1, columns: %2, row width: %3, data alignment: %4")
        .arg(static_cast<qulonglong>(table.row_count()))
        .arg(static_cast<qulonglong>(table.column_count()))
        .arg(table.row_width())
        .arg(table.data_alignment()));
    lines.push_back(QCoreApplication::translate("Utf.UtfEditUi", "Version: %1, table size: %2")
        .arg(table.version())
        .arg(static_cast<qulonglong>(table.table_size())));
    if (table.text_encoding()) {
        lines.push_back(QCoreApplication::translate("Utf.UtfEditUi", "Text encoding: %1").arg(utf8_to_qstring(*table.text_encoding())));
    }

    QStringList columns;
    columns.reserve(static_cast<qsizetype>(table.column_count()));
    for (uint32_t col = 0; col < table.column_count(); ++col) {
        const auto& column = table.column(col);
        columns.push_back(QStringLiteral("%1 (%2/%3)")
            .arg(utf8_to_qstring(column.name))
            .arg(utf_type_name(column.type))
            .arg(utf_flag_name(column.flag)));
    }
    lines.push_back(QCoreApplication::translate("Utf.UtfEditUi", "Columns: %1").arg(columns.join(QStringLiteral(" | "))));

    const auto rows_to_show = std::min<uint32_t>(table.row_count(), static_cast<uint32_t>(max_rows));
    for (uint32_t row = 0; row < rows_to_show; ++row) {
        QStringList cells;
        cells.reserve(static_cast<qsizetype>(table.column_count()));
        for (uint32_t col = 0; col < table.column_count(); ++col) {
            const auto& column = table.column(col);
            auto value = table.get_value(row, col);
            QString text = value ? utf_value_text(*value) : utf8_to_qstring(value.error());
            if (text.size() > max_value_chars) {
                text = text.left(max_value_chars) + QStringLiteral("...");
            }
            cells.push_back(QStringLiteral("%1=%2").arg(utf8_to_qstring(column.name), text));
        }
        lines.push_back(QStringLiteral("[%1] %2").arg(static_cast<qulonglong>(row)).arg(cells.join(QStringLiteral("; "))));
    }
    if (table.row_count() > rows_to_show) {
        lines.push_back(QCoreApplication::translate(
            "Utf.UtfEditUi",
            "... %n more row(s)",
            nullptr,
            static_cast<int>(table.row_count() - rows_to_show)));
    }
    return lines.join(QLatin1Char('\n'));
}

std::expected<std::vector<uint8_t>, QString> parse_hex_bytes(QString text, size_t fixed_size) {
    text.remove(QLatin1Char(' '));
    text.remove(QLatin1Char('\t'));
    text.remove(QLatin1Char('\n'));
    text.remove(QLatin1Char('\r'));
    if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        text = text.mid(2);
    }
    if (text.size() % 2 != 0) {
        return std::unexpected(QCoreApplication::translate("Utf.UtfEditUi", "hex byte text must contain an even number of digits"));
    }
    if (fixed_size != 0 && static_cast<size_t>(text.size() / 2) != fixed_size) {
        return std::unexpected(QCoreApplication::translate("Utf.UtfEditUi", "hex value must be exactly %1 bytes").arg(fixed_size));
    }
    std::vector<uint8_t> bytes;
    bytes.reserve(static_cast<size_t>(text.size() / 2));
    for (int i = 0; i < text.size(); i += 2) {
        bool ok = false;
        const auto byte = text.mid(i, 2).toUInt(&ok, 16);
        if (!ok || byte > 0xFF) {
            return std::unexpected(QCoreApplication::translate("Utf.UtfEditUi", "hex byte text contains invalid digits"));
        }
        bytes.push_back(static_cast<uint8_t>(byte));
    }
    return bytes;
}

std::expected<cricodecs::utf::Value, QString> parse_utf_value(
    cricodecs::utf::ColumnType type,
    const QString& text
) {
    using namespace cricodecs::utf;
    bool ok = false;
    switch (type) {
    case ColumnType::UInt8: {
        const auto value = text.toUInt(&ok, 0);
        if (!ok || value > std::numeric_limits<uint8_t>::max()) return std::unexpected(QCoreApplication::translate("Utf.UtfEditUi", "value must fit in u8"));
        return static_cast<uint8_t>(value);
    }
    case ColumnType::SInt8: {
        const auto value = text.toInt(&ok, 0);
        if (!ok || value < std::numeric_limits<int8_t>::min() || value > std::numeric_limits<int8_t>::max()) return std::unexpected(QCoreApplication::translate("Utf.UtfEditUi", "value must fit in s8"));
        return static_cast<int8_t>(value);
    }
    case ColumnType::UInt16: {
        const auto value = text.toUInt(&ok, 0);
        if (!ok || value > std::numeric_limits<uint16_t>::max()) return std::unexpected(QCoreApplication::translate("Utf.UtfEditUi", "value must fit in u16"));
        return static_cast<uint16_t>(value);
    }
    case ColumnType::SInt16: {
        const auto value = text.toInt(&ok, 0);
        if (!ok || value < std::numeric_limits<int16_t>::min() || value > std::numeric_limits<int16_t>::max()) return std::unexpected(QCoreApplication::translate("Utf.UtfEditUi", "value must fit in s16"));
        return static_cast<int16_t>(value);
    }
    case ColumnType::UInt32: {
        const auto value = text.toUInt(&ok, 0);
        if (!ok) return std::unexpected(QCoreApplication::translate("Utf.UtfEditUi", "value must fit in u32"));
        return static_cast<uint32_t>(value);
    }
    case ColumnType::SInt32: {
        const auto value = text.toInt(&ok, 0);
        if (!ok) return std::unexpected(QCoreApplication::translate("Utf.UtfEditUi", "value must fit in s32"));
        return static_cast<int32_t>(value);
    }
    case ColumnType::UInt64: {
        const auto value = text.toULongLong(&ok, 0);
        if (!ok) return std::unexpected(QCoreApplication::translate("Utf.UtfEditUi", "value must fit in u64"));
        return static_cast<uint64_t>(value);
    }
    case ColumnType::SInt64: {
        const auto value = text.toLongLong(&ok, 0);
        if (!ok) return std::unexpected(QCoreApplication::translate("Utf.UtfEditUi", "value must fit in s64"));
        return static_cast<int64_t>(value);
    }
    case ColumnType::Float: {
        const auto value = text.toFloat(&ok);
        if (!ok) return std::unexpected(QCoreApplication::translate("Utf.UtfEditUi", "value must be a float"));
        return value;
    }
    case ColumnType::Double: {
        const auto value = text.toDouble(&ok);
        if (!ok) return std::unexpected(QCoreApplication::translate("Utf.UtfEditUi", "value must be a double"));
        return value;
    }
    case ColumnType::String:
        return qstring_to_utf8(text);
    case ColumnType::VLData: {
        auto bytes = parse_hex_bytes(text);
        if (!bytes) return std::unexpected(bytes.error());
        return std::move(*bytes);
    }
    case ColumnType::GUID: {
        auto bytes = parse_hex_bytes(text, 16);
        if (!bytes) return std::unexpected(bytes.error());
        GUID guid{};
        std::copy(bytes->begin(), bytes->end(), guid.data);
        return guid;
    }
    }
    return std::unexpected(QCoreApplication::translate("Utf.UtfEditUi", "unsupported UTF column type"));
}

UtfEditResult rename_table(cricodecs::utf::UtfTable& utf, QString name) {
    name = name.trimmed();
    if (name.isEmpty()) {
        return {
            .handled = true,
            .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "Rename failed"),
            .error = QCoreApplication::translate("Utf.UtfEditUi", "UTF table name cannot be empty.")
        };
    }

    auto result = modules::utf::rename_table(utf, qstring_to_utf8(name));
    if (!result) {
        return {
            .handled = true,
            .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "Rename failed"),
            .error = utf8_to_qstring(result.error())
        };
    }
    return {
        .handled = true,
        .changed = true,
        .change_message = QCoreApplication::translate("Utf.UtfEditUi", "Renamed UTF table to %1.").arg(name),
        .title = name
    };
}

UtfEditResult set_cell_value(cricodecs::utf::UtfTable& utf, int row, int column, QString text) {
    if (row < 0 || column < 0 ||
        row >= static_cast<int>(utf.row_count()) ||
        column >= static_cast<int>(utf.column_count())) {
        return {};
    }

    const auto type = utf.column(static_cast<uint32_t>(column)).type;
    auto value = parse_utf_value(type, text);
    if (!value) {
        return {
            .handled = true,
            .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "Value edit failed"),
            .error = value.error()
        };
    }
    auto result = modules::utf::set_value(utf, static_cast<uint32_t>(row), static_cast<uint32_t>(column), std::move(*value));
    if (!result) {
        return {
            .handled = true,
            .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "Value edit failed"),
            .error = utf8_to_qstring(result.error())
        };
    }
    return {
        .handled = true,
        .changed = true,
        .change_message = QCoreApplication::translate("Utf.UtfEditUi", "Changed UTF value at row %1, column %2.").arg(row).arg(column)
    };
}

UtfEditResult edit_grid_item(cricodecs::utf::UtfTable& utf, int row, int column, QString text) {
    if (row < 0 || column < 0 ||
        row >= static_cast<int>(utf.row_count()) ||
        column >= static_cast<int>(utf.column_count())) {
        return {};
    }
    if (utf.column(static_cast<uint32_t>(column)).type == cricodecs::utf::ColumnType::VLData) {
        return {
            .handled = true,
            .show_cell = true,
            .row = row,
            .column = column
        };
    }
    auto result = set_cell_value(utf, row, column, std::move(text));
    if (result.changed) {
        result.change_message = QCoreApplication::translate("Utf.UtfEditUi", "Changed UTF grid value at row %1, column %2.").arg(row).arg(column);
    }
    result.refresh = !result.error.isEmpty();
    return result;
}

UtfEditResult edit_schema_item(cricodecs::utf::UtfTable& utf, int row, int column, QString text) {
    const auto col = static_cast<uint32_t>(row);
    if (row < 0 || col >= utf.column_count()) {
        return {};
    }

    if (column == 0) {
        auto result = modules::utf::rename_column(utf, col, qstring_to_utf8(text));
        if (!result) {
            return {
                .handled = true,
                .refresh = true,
                .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "Schema edit failed"),
                .error = utf8_to_qstring(result.error())
            };
        }
        return {
            .handled = true,
            .changed = true,
            .change_message = QCoreApplication::translate("Utf.UtfEditUi", "Renamed UTF column %1.").arg(col)
        };
    }

    if (column == 1) {
        auto type = utf_type_from_name(text);
        if (!type) {
            return {
                .handled = true,
                .refresh = true,
                .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "Schema edit failed"),
                .error = QCoreApplication::translate("Utf.UtfEditUi", "Unsupported UTF column type.")
            };
        }
        auto result = modules::utf::set_column_type(utf, col, *type);
        if (!result) {
            return {
                .handled = true,
                .refresh = true,
                .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "Schema edit failed"),
                .error = utf8_to_qstring(result.error())
            };
        }
        return {
            .handled = true,
            .changed = true,
            .change_message = QCoreApplication::translate("Utf.UtfEditUi", "Changed UTF column %1 type. Existing values were cleared for that column.").arg(col)
        };
    }

    if (column == 2) {
        auto flag = utf_flag_from_name(text);
        if (!flag) {
            return {
                .handled = true,
                .refresh = true,
                .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "Schema edit failed"),
                .error = QCoreApplication::translate("Utf.UtfEditUi", "Flags must be name, name|row, name|default, or name|default|row.")
            };
        }
        auto result = modules::utf::set_column_flag(utf, col, *flag);
        if (!result) {
            return {
                .handled = true,
                .refresh = true,
                .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "Schema edit failed"),
                .error = utf8_to_qstring(result.error())
            };
        }
        return {
            .handled = true,
            .changed = true,
            .change_message = QCoreApplication::translate("Utf.UtfEditUi", "Changed UTF column %1 flags.").arg(col)
        };
    }

    if (column == 3) {
        const auto& utf_column = utf.column(col);
        auto value = parse_utf_value(utf_column.type, text);
        if (!value) {
            return {
                .handled = true,
                .refresh = true,
                .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "Schema default edit failed"),
                .error = value.error()
            };
        }
        auto result = modules::utf::set_default_value(utf, col, std::move(*value));
        if (!result) {
            return {
                .handled = true,
                .refresh = true,
                .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "Schema edit failed"),
                .error = utf8_to_qstring(result.error())
            };
        }
        return {
            .handled = true,
            .changed = true,
            .change_message = QCoreApplication::translate("Utf.UtfEditUi", "Changed UTF default value for column %1.").arg(col)
        };
    }

    return {.handled = true};
}

UtfEditResult add_row_action(cricodecs::utf::UtfTable& utf) {
    const auto row = modules::utf::add_row(utf);
    return {
        .handled = true,
        .changed = true,
        .change_message = QCoreApplication::translate("Utf.UtfEditUi", "Added UTF row %1.").arg(row)
    };
}

UtfEditResult remove_row(cricodecs::utf::UtfTable& utf, int row) {
    if (row < 0 || row >= static_cast<int>(utf.row_count())) {
        return {};
    }
    auto result = modules::utf::remove_row(utf, static_cast<uint32_t>(row));
    if (!result) {
        return {
            .handled = true,
            .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "Remove row failed"),
            .error = utf8_to_qstring(result.error())
        };
    }
    return {
        .handled = true,
        .changed = true,
        .change_message = QCoreApplication::translate("Utf.UtfEditUi", "Removed UTF row %1.").arg(row)
    };
}

UtfEditResult add_column(cricodecs::utf::UtfTable& utf, QString name, QString type_text) {
    name = name.trimmed();
    const auto type = utf_type_from_name(type_text);
    if (name.isEmpty() || !type) {
        return {};
    }
    modules::utf::add_column(utf, qstring_to_utf8(name), *type);
    return {
        .handled = true,
        .changed = true,
        .change_message = QCoreApplication::translate("Utf.UtfEditUi", "Added UTF column %1.").arg(name)
    };
}

UtfEditResult remove_column(cricodecs::utf::UtfTable& utf, int column) {
    if (column < 0 || column >= static_cast<int>(utf.column_count())) {
        return {};
    }
    const auto name = utf8_to_qstring(utf.column(static_cast<uint32_t>(column)).name);
    auto result = modules::utf::remove_column(utf, static_cast<uint32_t>(column));
    if (!result) {
        return {
            .handled = true,
            .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "Remove column failed"),
            .error = utf8_to_qstring(result.error())
        };
    }
    return {
        .handled = true,
        .changed = true,
        .change_message = QCoreApplication::translate("Utf.UtfEditUi", "Removed UTF column %1.").arg(name)
    };
}

UtfEditResult rename_column(cricodecs::utf::UtfTable& utf, int column, QString name) {
    if (column < 0 || column >= static_cast<int>(utf.column_count()) || name.trimmed().isEmpty()) {
        return {};
    }
    auto result = modules::utf::rename_column(utf, static_cast<uint32_t>(column), qstring_to_utf8(name));
    if (!result) {
        return {
            .handled = true,
            .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "Rename column failed"),
            .error = utf8_to_qstring(result.error())
        };
    }
    return {
        .handled = true,
        .changed = true,
        .change_message = QCoreApplication::translate("Utf.UtfEditUi", "Renamed UTF column %1 to %2.").arg(column).arg(name)
    };
}

UtfEditResult replace_binary_cell(
    cricodecs::utf::UtfTable& utf,
    int row,
    int column,
    std::vector<uint8_t> bytes
) {
    if (row < 0 || column < 0 ||
        row >= static_cast<int>(utf.row_count()) ||
        column >= static_cast<int>(utf.column_count()) ||
        utf.column(static_cast<uint32_t>(column)).type != cricodecs::utf::ColumnType::VLData) {
        return {
            .handled = true,
            .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "No binary cell selected"),
            .error = QCoreApplication::translate("Utf.UtfEditUi", "Select a UTF binary/VLData cell first.")
        };
    }
    auto result = modules::utf::set_value(utf, static_cast<uint32_t>(row), static_cast<uint32_t>(column), std::move(bytes));
    if (!result) {
        return {
            .handled = true,
            .warning_title = QCoreApplication::translate("Utf.UtfEditUi", "Replacement failed"),
            .error = utf8_to_qstring(result.error())
        };
    }
    return {
        .handled = true,
        .changed = true,
        .change_message = QCoreApplication::translate("Utf.UtfEditUi", "Replaced UTF binary cell row %1 column %2.").arg(row).arg(column)
    };
}

} // namespace cristudio::modules::utf
