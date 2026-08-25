#include "modules/cpk/cpk_edit_ui.hpp"

#include "editor/editor_helpers.hpp"
#include "editor/editor_widgets.hpp"
#include "editor/table_item_helpers.hpp"
#include "modules/ui_value_helpers.hpp"
#include "path_text.hpp"

#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>

namespace cristudio::modules::cpk {
namespace {

class CompressionStateDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const auto state = index.data(Qt::CheckStateRole);
        if (!state.isValid()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        auto background = option;
        background.features &= ~QStyleOptionViewItem::HasCheckIndicator;
        background.text.clear();
        QStyledItemDelegate::paint(painter, background, index);

        const bool checked = state.toInt() == Qt::Checked;
        constexpr int toggle_width = 42;
        constexpr int toggle_height = 20;
        const QRect toggle_rect(
            option.rect.center().x() - toggle_width / 2,
            option.rect.center().y() - toggle_height / 2,
            toggle_width,
            toggle_height);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const auto& palette = option.palette;
        painter->setPen(palette.color(QPalette::Mid));
        painter->setBrush(checked ? palette.color(QPalette::Highlight) : palette.color(QPalette::Button));
        painter->drawRoundedRect(toggle_rect, 4, 4);
        painter->setPen(checked ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::ButtonText));
        painter->drawText(toggle_rect, Qt::AlignCenter, checked ? QStringLiteral("On") : QStringLiteral("Off"));
        painter->restore();
    }

    bool editorEvent(
        QEvent* event,
        QAbstractItemModel* model,
        const QStyleOptionViewItem& option,
        const QModelIndex& index
    ) override {
        if (!(index.flags() & Qt::ItemIsEnabled) || !(index.flags() & Qt::ItemIsUserCheckable)) {
            return false;
        }
        bool activate = false;
        if (event->type() == QEvent::MouseButtonRelease) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            activate = mouse->button() == Qt::LeftButton && option.rect.contains(mouse->position().toPoint());
        } else if (event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            activate = key->key() == Qt::Key_Space || key->key() == Qt::Key_Select;
        }
        if (!activate) {
            return false;
        }
        const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        return model->setData(index, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    }
};

int optional_bool_index(const std::optional<bool>& value) {
    if (!value.has_value()) {
        return 0;
    }
    return *value ? 2 : 1;
}

std::optional<bool> optional_bool_from_index(int index) {
    if (index == 1) {
        return false;
    }
    if (index == 2) {
        return true;
    }
    return std::nullopt;
}

QComboBox* make_chunk_combo(const std::optional<bool>& value, QWidget* parent) {
    auto* combo = new QComboBox(parent);
    combo->addItem(QCoreApplication::translate("Cpk.CpkEditUi", "Auto from preset"));
    combo->addItem(QCoreApplication::translate("Cpk.CpkEditUi", "Force off"));
    combo->addItem(QCoreApplication::translate("Cpk.CpkEditUi", "Force on"));
    combo->setCurrentIndex(optional_bool_index(value));
    return combo;
}

QWidget* switch_row(ToggleSwitch*& control, QString text, QString tooltip, QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(10);
    control = new ToggleSwitch(row);
    control->setToolTip(tooltip);
    auto* label = new QLabel(std::move(text), row);
    label->setToolTip(tooltip);
    layout->addWidget(control);
    layout->addWidget(label, 1);
    return row;
}

} // namespace

void configure_editor_archive_table(QTableWidget* table) {
    table->setItemDelegateForColumn(12, new CompressionStateDelegate(table));
}

void populate_editor_archive_table(QTableWidget* table, const cricodecs::cpk::Cpk& cpk) {
    const QSignalBlocker signal_blocker(table);
    const bool sorting_enabled = table->isSortingEnabled();
    const bool updates_enabled = table->updatesEnabled();
    table->setSortingEnabled(false);
    table->setUpdatesEnabled(false);
    table->clear();
    table->setColumnCount(14);
    table->setHorizontalHeaderLabels({
        QCoreApplication::translate("Cpk.CpkEditUi", "Index"),
        QCoreApplication::translate("Cpk.CpkEditUi", "Full Path"),
        QCoreApplication::translate("Cpk.CpkEditUi", "Dirname"),
        QCoreApplication::translate("Cpk.CpkEditUi", "Dirname Raw"),
        QCoreApplication::translate("Cpk.CpkEditUi", "Filename"),
        QCoreApplication::translate("Cpk.CpkEditUi", "Filename Raw"),
        QStringLiteral("ID"),
        QCoreApplication::translate("Cpk.CpkEditUi", "TOC Index"),
        QCoreApplication::translate("Cpk.CpkEditUi", "Offset"),
        QCoreApplication::translate("Cpk.CpkEditUi", "File Size"),
        QCoreApplication::translate("Cpk.CpkEditUi", "Extract Size"),
        QStringLiteral("Compressed"),
        QCoreApplication::translate("Cpk.CpkEditUi", "Compress On Save"),
        QCoreApplication::translate("Cpk.CpkEditUi", "User String")
    });
    table->setRowCount(static_cast<int>(cpk.file_count()));
    for (size_t index = 0; index < cpk.files().size(); ++index) {
        const auto& entry = cpk.files()[index];
        const auto row = static_cast<int>(index);
        set_table_item(table, row, 0, QString::number(static_cast<qulonglong>(index)), false);
        set_table_item(table, row, 1, path_to_qstring(entry.full_path()), true);
        set_table_item(table, row, 2, utf8_to_qstring(entry.dirname), true);
        set_table_item(table, row, 3, utf8_to_qstring(entry.dirname_raw), false);
        set_table_item(table, row, 4, utf8_to_qstring(entry.filename), true);
        set_table_item(table, row, 5, utf8_to_qstring(entry.filename_raw), false);
        set_table_item(table, row, 6, QString::number(entry.id), true);
        set_table_item(table, row, 7, QString::number(entry.toc_index), false);
        set_table_item(table, row, 8, QStringLiteral("0x%1").arg(entry.file_offset, 0, 16).toUpper(), false);
        set_table_item(table, row, 9, QString::number(static_cast<qulonglong>(entry.file_size)), false);
        set_table_item(table, row, 10, QString::number(static_cast<qulonglong>(entry.extract_size)), false);
        set_table_item(table, row, 11, entry.is_compressed ? QStringLiteral("yes") : QStringLiteral("no"), false);
        auto* compress_item = new QTableWidgetItem();
        compress_item->setFlags(
            (compress_item->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        compress_item->setCheckState(entry.request_compress ? Qt::Checked : Qt::Unchecked);
        compress_item->setTextAlignment(Qt::AlignCenter);
        compress_item->setToolTip(QCoreApplication::translate("Cpk.CpkEditUi", "Compress this entry with CRILAYLA on save when compression reduces its size."));
        table->setItem(row, 12, compress_item);
        set_table_item(table, row, 13, utf8_to_qstring(entry.user_string), true);
    }
    table->setSortingEnabled(sorting_enabled);
    table->setUpdatesEnabled(updates_enabled);
}

std::expected<std::optional<EntryProperties>, QString> choose_entry_properties(
    QWidget* parent,
    const cricodecs::cpk::CpkEntry& entry
) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Cpk.CpkEditUi", "CPK entry properties"));
    dialog.setMinimumWidth(520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* path_edit = new QLineEdit(path_to_qstring(entry.full_path()), &dialog);
    auto* dirname_edit = new QLineEdit(utf8_to_qstring(entry.dirname), &dialog);
    auto* filename_edit = new QLineEdit(utf8_to_qstring(entry.filename), &dialog);
    auto* id_edit = make_unsigned_integer_edit(
        entry.id, 0, std::numeric_limits<uint32_t>::max(), &dialog, QCoreApplication::translate("Cpk.CpkEditUi", "CPK entry ID"));
    ToggleSwitch* compress_switch = nullptr;
    auto* compression_row = switch_row(
        compress_switch,
        QCoreApplication::translate("Cpk.CpkEditUi", "Compress this entry on save"),
        QCoreApplication::translate("Cpk.CpkEditUi", "Use CRILAYLA when it makes this entry smaller."),
        &dialog);
    compress_switch->setAccessibleName(QCoreApplication::translate("Cpk.CpkEditUi", "Compress this CPK entry on save"));
    compress_switch->setChecked(entry.request_compress);
    auto* user_string_edit = new QLineEdit(utf8_to_qstring(entry.user_string), &dialog);

    form->addRow(QCoreApplication::translate("Cpk.CpkEditUi", "Full path"), path_edit);
    form->addRow(QCoreApplication::translate("Cpk.CpkEditUi", "DirName"), dirname_edit);
    form->addRow(QCoreApplication::translate("Cpk.CpkEditUi", "FileName"), filename_edit);
    form->addRow(QStringLiteral("ID"), id_edit);
    form->addRow(QCoreApplication::translate("Cpk.CpkEditUi", "Compression"), compression_row);
    form->addRow(QCoreApplication::translate("Cpk.CpkEditUi", "User string"), user_string_edit);
    layout->addLayout(form);

    auto* buttons = dialog_buttons(dialog);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return std::optional<EntryProperties>{};
    }

    const auto id = unsigned_integer_value(
        id_edit, 0, std::numeric_limits<uint32_t>::max(), QStringLiteral("ID"));
    if (!id) {
        return std::unexpected(id.error());
    }
    auto dirname = dirname_edit->text().trimmed();
    auto filename = filename_edit->text().trimmed();
    if (filename.isEmpty() || filename.contains(QLatin1Char('/')) || filename.contains(QLatin1Char('\\'))) {
        return std::unexpected(QCoreApplication::translate("Cpk.CpkEditUi", "FileName must be a non-empty leaf name without path separators."));
    }
    auto full_path = path_edit->text().trimmed();
    if (dirname != utf8_to_qstring(entry.dirname) || filename != utf8_to_qstring(entry.filename)) {
        dirname.replace(QLatin1Char('\\'), QLatin1Char('/'));
        while (dirname.endsWith(QLatin1Char('/'))) {
            dirname.chop(1);
        }
        full_path = dirname.isEmpty() ? filename : dirname + QLatin1Char('/') + filename;
    }

    return EntryProperties{
        .full_path = qstring_to_utf8(full_path),
        .dirname = qstring_to_utf8(dirname),
        .filename = qstring_to_utf8(filename),
        .id = static_cast<uint32_t>(*id),
        .request_compress = compress_switch->isChecked(),
        .user_string = qstring_to_utf8(user_string_edit->text())
    };
}

std::optional<BuildOptionsSelection> choose_build_options(
    QWidget* parent,
    const cricodecs::cpk::Cpk& cpk,
    bool obfuscate_utf
) {
    auto options = cpk.options();
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Cpk.CpkEditUi", "CPK options"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* preset_combo = new QComboBox(&dialog);
    const std::array presets = {
        cricodecs::cpk::CpkPreset::Custom,
        cricodecs::cpk::CpkPreset::Id,
        cricodecs::cpk::CpkPreset::Filename,
        cricodecs::cpk::CpkPreset::FilenameId,
        cricodecs::cpk::CpkPreset::FilenameGroup,
        cricodecs::cpk::CpkPreset::IdGroup,
        cricodecs::cpk::CpkPreset::FilenameIdGroup
    };
    int preset_index = 0;
    for (const auto preset : presets) {
        preset_combo->addItem(cpk_preset_name(preset), static_cast<int>(preset));
        if (preset == options.preset) {
            preset_index = preset_combo->count() - 1;
        }
    }
    preset_combo->setCurrentIndex(preset_index);

    auto* align_spin = new QSpinBox(&dialog);
    align_spin->setRange(1, std::numeric_limits<uint16_t>::max());
    align_spin->setValue(options.align);
    align_spin->setSuffix(QCoreApplication::translate("Cpk.CpkEditUi", " bytes"));

    auto* crc_check = new QCheckBox(QCoreApplication::translate("Cpk.CpkEditUi", "Emit standard CPK CRC tables and row CRCs"), &dialog);
    crc_check->setChecked(options.enable_crc);

    ToggleSwitch* obfuscate_switch = nullptr;
    auto* obfuscate_row = switch_row(
        obfuscate_switch,
        QCoreApplication::translate("Cpk.CpkEditUi", "Encrypt CPK UTF tables on save"),
        QCoreApplication::translate("Cpk.CpkEditUi", "Apply the standard CPK UTF XOR transform to archive metadata."),
        &dialog);
    obfuscate_switch->setAccessibleName(QCoreApplication::translate("Cpk.CpkEditUi", "Encrypt CPK UTF tables on save"));
    obfuscate_switch->setChecked(obfuscate_utf);

    auto* toc_combo = make_chunk_combo(options.enable_toc, &dialog);
    auto* itoc_combo = make_chunk_combo(options.enable_itoc, &dialog);
    auto* gtoc_combo = make_chunk_combo(options.enable_gtoc, &dialog);
    auto* etoc_combo = make_chunk_combo(options.enable_etoc, &dialog);

    auto* encoding_combo = new QComboBox(&dialog);
    encoding_combo->setEditable(true);
    encoding_combo->addItem(QCoreApplication::translate("Cpk.CpkEditUi", "Auto (system)"), QString{});
    for (const auto& encoding : {
        QStringLiteral("UTF-8"), QStringLiteral("GBK"), QStringLiteral("CP936"),
        QStringLiteral("CP932"), QStringLiteral("Shift-JIS"), QStringLiteral("SJIS")}) {
        encoding_combo->addItem(encoding, encoding);
    }
    if (options.encoding.encoding) {
        const auto current = utf8_to_qstring(*options.encoding.encoding);
        if (const auto index = encoding_combo->findData(current); index >= 0) {
            encoding_combo->setCurrentIndex(index);
        } else {
            encoding_combo->setEditText(current);
        }
    } else {
        encoding_combo->setCurrentIndex(0);
    }
    auto* tver_edit = new QLineEdit(utf8_to_qstring(options.tver), &dialog);
    auto* comment_edit = new QLineEdit(utf8_to_qstring(options.comment), &dialog);
    auto* local_dir_edit = new QLineEdit(utf8_to_qstring(options.etoc_local_dir), &dialog);

    form->addRow(QCoreApplication::translate("Cpk.CpkEditUi", "Declared preset"), preset_combo);
    form->addRow(QCoreApplication::translate("Cpk.CpkEditUi", "TOC chunk"), toc_combo);
    form->addRow(QCoreApplication::translate("Cpk.CpkEditUi", "ITOC chunk"), itoc_combo);
    form->addRow(QCoreApplication::translate("Cpk.CpkEditUi", "GTOC chunk"), gtoc_combo);
    form->addRow(QCoreApplication::translate("Cpk.CpkEditUi", "ETOC chunk"), etoc_combo);
    form->addRow(QCoreApplication::translate("Cpk.CpkEditUi", "Alignment"), align_spin);
    form->addRow(QStringLiteral("CRC"), crc_check);
    form->addRow(QCoreApplication::translate("Cpk.CpkEditUi", "UTF metadata"), obfuscate_row);
    form->addRow(QCoreApplication::translate("Cpk.CpkEditUi", "Text encoding"), encoding_combo);
    form->addRow(QStringLiteral("TVER"), tver_edit);
    form->addRow(QCoreApplication::translate("Cpk.CpkEditUi", "Comment"), comment_edit);
    form->addRow(QCoreApplication::translate("Cpk.CpkEditUi", "ETOC LocalDir"), local_dir_edit);
    layout->addLayout(form);

    auto* note = dim_label(
        QCoreApplication::translate("Cpk.CpkEditUi", "Chunk controls use native CpkOptions overrides. Compression requests use CRILAYLA only when the result is smaller than the original entry."),
        &dialog
    );
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = dialog_buttons(dialog);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    options.preset = static_cast<cricodecs::cpk::CpkPreset>(preset_combo->currentData().toInt());
    options.enable_toc = optional_bool_from_index(toc_combo->currentIndex());
    options.enable_itoc = optional_bool_from_index(itoc_combo->currentIndex());
    options.enable_gtoc = optional_bool_from_index(gtoc_combo->currentIndex());
    options.enable_etoc = optional_bool_from_index(etoc_combo->currentIndex());
    options.enable_crc = crc_check->isChecked();
    options.align = static_cast<uint16_t>(align_spin->value());
    const auto encoding_text = encoding_combo->currentIndex() == 0
        ? QString{}
        : encoding_combo->currentText().trimmed();
    options.encoding.encoding = encoding_text.isEmpty()
        ? std::optional<std::string>{}
        : std::optional<std::string>{qstring_to_utf8(encoding_text)};
    options.comment = qstring_to_utf8(comment_edit->text());
    options.tver = qstring_to_utf8(tver_edit->text());
    options.etoc_local_dir = qstring_to_utf8(local_dir_edit->text());
    return BuildOptionsSelection{
        .options = std::move(options),
        .obfuscate_utf = obfuscate_switch->isChecked(),
    };
}

} // namespace cristudio::modules::cpk
