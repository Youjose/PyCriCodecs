#include "modules/afs/afs_edit_ui.hpp"

#include "editor/editor_helpers.hpp"
#include "editor/editor_widgets.hpp"
#include "editor/table_item_helpers.hpp"
#include "modules/ui_value_helpers.hpp"
#include "path_text.hpp"
#include "shared/document_extract_helpers.hpp"

#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTime>
#include <QTimeEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace cristudio::modules::afs {
namespace {

uint32_t clamp_spin_value(uint32_t value) {
    return std::min<uint32_t>(value, static_cast<uint32_t>(std::numeric_limits<int>::max()));
}

QString header_mode_name(cricodecs::afs::AfsHeaderNameMode mode) {
    switch (mode) {
    case cricodecs::afs::AfsHeaderNameMode::filename_only: return QCoreApplication::translate("Afs.AfsEditUi", "Only Filename");
    case cricodecs::afs::AfsHeaderNameMode::cut_overlapping_string: return QCoreApplication::translate("Afs.AfsEditUi", "Cut Overlapping String");
    case cricodecs::afs::AfsHeaderNameMode::full_path: return QCoreApplication::translate("Afs.AfsEditUi", "Full Path");
    }
    return QCoreApplication::translate("Afs.AfsEditUi", "Only Filename");
}

QString entry_type_name(cricodecs::afs::AfsEntryType type) {
    switch (type) {
    case cricodecs::afs::AfsEntryType::adx: return QStringLiteral("adx");
    case cricodecs::afs::AfsEntryType::ogg: return QStringLiteral("ogg");
    case cricodecs::afs::AfsEntryType::hca: return QStringLiteral("hca");
    case cricodecs::afs::AfsEntryType::unknown: break;
    }
    return QStringLiteral("unknown");
}

QString optional_text(const std::optional<std::string>& value) {
    return value ? utf8_to_qstring(*value) : QString{};
}

QString timestamp_text(const cricodecs::afs::AfsEntry& entry) {
    const auto timestamp = entry.directory_timestamp();
    if (!timestamp) {
        return QStringLiteral("-");
    }
    return QStringLiteral("%1-%2-%3 %4:%5:%6")
        .arg(timestamp->year, 4, 10, QLatin1Char('0'))
        .arg(timestamp->month, 2, 10, QLatin1Char('0'))
        .arg(timestamp->day, 2, 10, QLatin1Char('0'))
        .arg(timestamp->hour, 2, 10, QLatin1Char('0'))
        .arg(timestamp->minute, 2, 10, QLatin1Char('0'))
        .arg(timestamp->second, 2, 10, QLatin1Char('0'));
}

} // namespace

void populate_editor_archive_table(QTableWidget* table, const cricodecs::afs::AfsContainer& afs) {
    table->clear();
    table->setColumnCount(10);
    table->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QCoreApplication::translate("Afs.AfsEditUi", "Present"),
        QCoreApplication::translate("Afs.AfsEditUi", "Name"),
        QCoreApplication::translate("Afs.AfsEditUi", "Type"),
        QCoreApplication::translate("Afs.AfsEditUi", "Offset"),
        QCoreApplication::translate("Afs.AfsEditUi", "Size"),
        QCoreApplication::translate("Afs.AfsEditUi", "Header Source"),
        QCoreApplication::translate("Afs.AfsEditUi", "Timestamp"),
        QCoreApplication::translate("Afs.AfsEditUi", "Metadata"),
        QCoreApplication::translate("Afs.AfsEditUi", "Suggested Path")
    });
    table->setRowCount(static_cast<int>(afs.entry_count()));
    for (const auto& entry : afs.entries()) {
        const auto row = static_cast<int>(entry.index);
        set_table_item(table, row, 0, QString::number(entry.index), false);
        set_table_item(table, row, 1, entry.present ? QStringLiteral("yes") : QStringLiteral("no"), false);
        set_table_item(table, row, 2, optional_text(entry.name), true);
        set_table_item(table, row, 3, entry_type_name(entry.type), false);
        set_table_item(table, row, 4, QStringLiteral("0x%1").arg(entry.offset, 0, 16).toUpper(), false);
        set_table_item(table, row, 5, QString::number(entry.size), false);
        set_table_item(table, row, 6, optional_text(entry.header_source_name), true);
        set_table_item(table, row, 7, timestamp_text(entry), false);
        set_table_item(table, row, 8, bytes_to_hex(entry.directory_metadata), true);
        set_table_item(table, row, 9, path_to_qstring(entry.suggested_path()), false);
    }
}

std::optional<AddFileOptions> choose_add_file_options(
    QWidget* parent,
    const cricodecs::afs::AfsContainer& afs,
    const QString& file_path
) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Afs.AfsEditUi", "Add AFS file"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* id_edit = make_unsigned_integer_edit(
        afs.entry_count(), 0, std::numeric_limits<uint32_t>::max(), &dialog, QCoreApplication::translate("Afs.AfsEditUi", "AFS file ID"));

    const auto default_name = path_from_qstring(file_path).filename().generic_string();
    auto* name_edit = new QLineEdit(utf8_to_qstring(default_name), &dialog);
    auto* header_source_edit = new QLineEdit(utf8_to_qstring(default_name), &dialog);
    auto* timestamp_check = new QCheckBox(QCoreApplication::translate("Afs.AfsEditUi", "Write directory timestamp"), &dialog);
    auto* date_edit = new QDateEdit(QDate::currentDate(), &dialog);
    date_edit->setCalendarPopup(true);
    auto* time_edit = new QTimeEdit(QTime::currentTime(), &dialog);
    date_edit->setEnabled(false);
    time_edit->setEnabled(false);
    QObject::connect(timestamp_check, &QCheckBox::toggled, date_edit, &QDateEdit::setEnabled);
    QObject::connect(timestamp_check, &QCheckBox::toggled, time_edit, &QTimeEdit::setEnabled);

    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "File"), new QLabel(file_path, &dialog));
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "File ID"), id_edit);
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Directory name"), name_edit);
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Header source"), header_source_edit);
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Timestamp"), timestamp_check);
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Date"), date_edit);
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Time"), time_edit);
    layout->addLayout(form);

    auto* note = dim_label(
        QCoreApplication::translate("Afs.AfsEditUi", "AFS file IDs are table slots. Adding at a later ID reserves empty slots before it; adding at an occupied ID replaces that slot."),
        &dialog
    );
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = dialog_buttons(
        dialog, QCoreApplication::translate("Afs.AfsEditUi", "Add"));
    bind_valid_inputs(*buttons->button(QDialogButtonBox::Ok), {id_edit});
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    AddFileOptions options;
    const auto file_id = unsigned_integer_value(
        id_edit, 0, std::numeric_limits<uint32_t>::max(), QCoreApplication::translate("Afs.AfsEditUi", "File ID"));
    if (!file_id) {
        return std::nullopt;
    }
    options.file_id = static_cast<uint32_t>(*file_id);
    auto name = qstring_to_utf8(name_edit->text().trimmed());
    if (!name.empty()) {
        options.name = std::move(name);
    }
    auto header_source = qstring_to_utf8(header_source_edit->text().trimmed());
    if (!header_source.empty()) {
        options.header_source_name = std::move(header_source);
    }
    if (timestamp_check->isChecked()) {
        const auto date = date_edit->date();
        const auto time = time_edit->time();
        options.directory_metadata = cricodecs::afs::encode_directory_timestamp(cricodecs::afs::AfsDirectoryTimestamp{
            .year = static_cast<uint16_t>(date.year()),
            .month = static_cast<uint16_t>(date.month()),
            .day = static_cast<uint16_t>(date.day()),
            .hour = static_cast<uint16_t>(time.hour()),
            .minute = static_cast<uint16_t>(time.minute()),
            .second = static_cast<uint16_t>(time.second())
        });
    }
    return options;
}

std::optional<uint32_t> choose_reserve_file_id(QWidget* parent, const cricodecs::afs::AfsContainer& afs) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Afs.AfsEditUi", "Reserve AFS file ID"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* id_edit = make_unsigned_integer_edit(
        afs.entry_count(), 0, std::numeric_limits<uint32_t>::max(), &dialog, QCoreApplication::translate("Afs.AfsEditUi", "Highest AFS file ID"));
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Highest file ID"), id_edit);
    layout->addLayout(form);

    auto* note = dim_label(
        QCoreApplication::translate("Afs.AfsEditUi", "Reserving a later ID creates empty AFS slots. It does not renumber existing entries."),
        &dialog
    );
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = dialog_buttons(
        dialog, QCoreApplication::translate("Afs.AfsEditUi", "Reserve"));
    bind_valid_inputs(*buttons->button(QDialogButtonBox::Ok), {id_edit});
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    const auto file_id = unsigned_integer_value(
        id_edit, 0, std::numeric_limits<uint32_t>::max(), QCoreApplication::translate("Afs.AfsEditUi", "Highest file ID"));
    return file_id ? std::optional<uint32_t>(static_cast<uint32_t>(*file_id)) : std::nullopt;
}

std::optional<std::optional<cricodecs::afs::AfsDirectoryTimestamp>> choose_directory_timestamp(
    QWidget* parent,
    const cricodecs::afs::AfsEntry& entry,
    uint32_t file_id
) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Afs.AfsEditUi", "Set AFS directory timestamp"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    const auto current = entry.directory_timestamp();
    auto* clear_check = new QCheckBox(QCoreApplication::translate("Afs.AfsEditUi", "Clear timestamp metadata"), &dialog);
    clear_check->setChecked(!current.has_value());

    const QDate date = current
        ? QDate(static_cast<int>(current->year), static_cast<int>(current->month), static_cast<int>(current->day))
        : QDate::currentDate();
    const QTime time = current
        ? QTime(static_cast<int>(current->hour), static_cast<int>(current->minute), static_cast<int>(current->second))
        : QTime::currentTime();

    auto* date_edit = new QDateEdit(date.isValid() ? date : QDate::currentDate(), &dialog);
    date_edit->setCalendarPopup(true);
    auto* time_edit = new QTimeEdit(time.isValid() ? time : QTime::currentTime(), &dialog);
    date_edit->setEnabled(!clear_check->isChecked());
    time_edit->setEnabled(!clear_check->isChecked());
    QObject::connect(clear_check, &QCheckBox::toggled, date_edit, [date_edit](bool checked) { date_edit->setEnabled(!checked); });
    QObject::connect(clear_check, &QCheckBox::toggled, time_edit, [time_edit](bool checked) { time_edit->setEnabled(!checked); });

    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "File ID"), new QLabel(QString::number(file_id), &dialog));
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Clear"), clear_check);
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Date"), date_edit);
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Time"), time_edit);
    layout->addLayout(form);

    auto* note = dim_label(
        QCoreApplication::translate("Afs.AfsEditUi", "The timestamp is stored in the 12-byte AFS directory metadata field; raw metadata remains editable in the table."),
        &dialog
    );
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = dialog_buttons(dialog);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    if (clear_check->isChecked()) {
        return std::optional<std::optional<cricodecs::afs::AfsDirectoryTimestamp>>{
            std::optional<cricodecs::afs::AfsDirectoryTimestamp>{}
        };
    }
    const auto selected_date = date_edit->date();
    const auto selected_time = time_edit->time();
    return std::optional<std::optional<cricodecs::afs::AfsDirectoryTimestamp>>{
        cricodecs::afs::AfsDirectoryTimestamp{
        .year = static_cast<uint16_t>(selected_date.year()),
        .month = static_cast<uint16_t>(selected_date.month()),
        .day = static_cast<uint16_t>(selected_date.day()),
        .hour = static_cast<uint16_t>(selected_time.hour()),
        .minute = static_cast<uint16_t>(selected_time.minute()),
        .second = static_cast<uint16_t>(selected_time.second())
        }
    };
}

std::optional<BuildOptions> choose_build_options(QWidget* parent, const cricodecs::afs::AfsContainer& afs) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Afs.AfsEditUi", "AFS options"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* align_spin = new QSpinBox(&dialog);
    align_spin->setRange(1, std::numeric_limits<int>::max());
    align_spin->setValue(static_cast<int>(clamp_spin_value(afs.alignment())));
    align_spin->setSuffix(QCoreApplication::translate("Afs.AfsEditUi", " bytes"));

    auto* directory_check = new QCheckBox(QCoreApplication::translate("Afs.AfsEditUi", "Write the optional 0x30-byte-per-entry directory table"), &dialog);
    directory_check->setChecked(afs.directory_table_enabled());

    auto* first_offset_check = new QCheckBox(QCoreApplication::translate("Afs.AfsEditUi", "Reserve an explicit first payload offset"), &dialog);
    first_offset_check->setChecked(afs.first_payload_offset().has_value());
    auto* first_offset_spin = new QSpinBox(&dialog);
    first_offset_spin->setRange(0, std::numeric_limits<int>::max());
    first_offset_spin->setValue(static_cast<int>(clamp_spin_value(afs.first_payload_offset().value_or(0))));
    first_offset_spin->setSuffix(QCoreApplication::translate("Afs.AfsEditUi", " bytes"));
    first_offset_spin->setEnabled(first_offset_check->isChecked());
    QObject::connect(first_offset_check, &QCheckBox::toggled, first_offset_spin, &QSpinBox::setEnabled);

    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Alignment"), align_spin);
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Directory table"), directory_check);
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "First payload offset"), first_offset_check);
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Offset value"), first_offset_spin);
    layout->addLayout(form);

    auto* note = dim_label(
        QCoreApplication::translate("Afs.AfsEditUi", "AFS keeps slot IDs stable. First payload offset is snapped to the selected alignment by the native builder."),
        &dialog
    );
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = dialog_buttons(dialog);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    return BuildOptions{
        .alignment = static_cast<uint32_t>(align_spin->value()),
        .directory_table_enabled = directory_check->isChecked(),
        .first_payload_offset = first_offset_check->isChecked()
            ? std::optional<uint32_t>(static_cast<uint32_t>(first_offset_spin->value()))
            : std::nullopt
    };
}

std::optional<AlsImportOptions> choose_als_import_options(
    QWidget* parent,
    const cricodecs::afs::AfsContainer* current_afs
) {
    const auto path_text = QFileDialog::getOpenFileName(
        parent,
        QCoreApplication::translate("Afs.AfsEditUi", "Import AFS file list"),
        QString{},
        QCoreApplication::translate("Afs.AfsEditUi", "AFS file lists (*.als);;All files (*)")
    );
    if (path_text.isEmpty()) {
        return std::nullopt;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Afs.AfsEditUi", "AFS ALS import"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* align_spin = new QSpinBox(&dialog);
    align_spin->setRange(1, std::numeric_limits<int>::max());
    align_spin->setValue(static_cast<int>(current_afs ? current_afs->alignment() : cricodecs::afs::AfsContainer::DEFAULT_ALIGNMENT));
    align_spin->setSuffix(QCoreApplication::translate("Afs.AfsEditUi", " bytes"));

    auto* directory_check = new QCheckBox(QCoreApplication::translate("Afs.AfsEditUi", "Write AFS directory table"), &dialog);
    directory_check->setChecked(current_afs ? current_afs->directory_table_enabled() : true);

    auto* source_root_edit = new QLineEdit(&dialog);
    source_root_edit->setPlaceholderText(QCoreApplication::translate("Afs.AfsEditUi", "Optional afslnk -dir lookup root"));
    auto* source_root_row = new QWidget(&dialog);
    auto* source_root_layout = new QHBoxLayout(source_root_row);
    source_root_layout->setContentsMargins(0, 0, 0, 0);
    source_root_layout->setSpacing(6);
    source_root_layout->addWidget(source_root_edit, 1);
    auto* browse_source_root = new QPushButton(QCoreApplication::translate("Afs.AfsEditUi", "Browse"), source_root_row);
    source_root_layout->addWidget(browse_source_root, 0);
    QObject::connect(browse_source_root, &QPushButton::clicked, &dialog, [&dialog, source_root_edit] {
        const auto dir = QFileDialog::getExistingDirectory(&dialog, QCoreApplication::translate("Afs.AfsEditUi", "Choose AFS source root"));
        if (!dir.isEmpty()) {
            source_root_edit->setText(dir);
        }
    });

    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "File list"), new QLabel(path_text, &dialog));
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Alignment"), align_spin);
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Directory table"), directory_check);
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Source root"), source_root_row);
    layout->addLayout(form);

    auto* note = dim_label(
        QCoreApplication::translate("Afs.AfsEditUi", "The native ALS importer supports :DIR=(dir) and sparse :(id number) commands, preserving header source text for later file-ID headers."),
        &dialog
    );
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = dialog_buttons(
        dialog, QCoreApplication::translate("Afs.AfsEditUi", "Import"));
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    AlsImportOptions options{
        .file_list_path = path_from_qstring(path_text),
        .alignment = static_cast<uint32_t>(align_spin->value()),
        .directory_table_enabled = directory_check->isChecked()
    };
    const auto source_root_text = source_root_edit->text().trimmed();
    if (!source_root_text.isEmpty()) {
        options.source_root = path_from_qstring(source_root_text);
    }
    return options;
}

std::expected<std::optional<std::filesystem::path>, QString> export_file_id_header(
    QWidget* parent,
    const cricodecs::afs::AfsContainer& afs,
    QString default_archive_name
) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Afs.AfsEditUi", "Export AFS file-ID header"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    if (default_archive_name.trimmed().isEmpty()) {
        default_archive_name = QStringLiteral("editor-output.afs");
    }
    auto* archive_name_edit = new QLineEdit(default_archive_name, &dialog);
    auto* prefix_edit = new QLineEdit(&dialog);
    auto* mode_combo = new QComboBox(&dialog);
    const std::array modes = {
        cricodecs::afs::AfsHeaderNameMode::filename_only,
        cricodecs::afs::AfsHeaderNameMode::cut_overlapping_string,
        cricodecs::afs::AfsHeaderNameMode::full_path
    };
    for (const auto mode : modes) {
        mode_combo->addItem(header_mode_name(mode), static_cast<int>(mode));
    }

    auto* preview = new QPlainTextEdit(&dialog);
    preview->setReadOnly(true);
    preview->setLineWrapMode(QPlainTextEdit::NoWrap);
    preview->setMinimumHeight(220);

    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Archive name"), archive_name_edit);
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "ID prefix"), prefix_edit);
    form->addRow(QCoreApplication::translate("Afs.AfsEditUi", "Name mode"), mode_combo);
    layout->addLayout(form);
    layout->addWidget(preview, 1);

    auto refresh_preview = [&afs, archive_name_edit, prefix_edit, mode_combo, preview] {
        const auto mode = static_cast<cricodecs::afs::AfsHeaderNameMode>(mode_combo->currentData().toInt());
        auto header = afs.build_file_id_header(
            qstring_to_utf8(archive_name_edit->text().trimmed()),
            qstring_to_utf8(prefix_edit->text()),
            mode
        );
        if (!header) {
            preview->setPlainText(QCoreApplication::translate("Afs.AfsEditUi", "Header preview failed: %1").arg(utf8_to_qstring(header.error())));
            return;
        }
        preview->setPlainText(utf8_to_qstring(*header));
    };
    QObject::connect(archive_name_edit, &QLineEdit::textChanged, &dialog, [&refresh_preview](const QString&) { refresh_preview(); });
    QObject::connect(prefix_edit, &QLineEdit::textChanged, &dialog, [&refresh_preview](const QString&) { refresh_preview(); });
    QObject::connect(mode_combo, &QComboBox::currentTextChanged, &dialog, [&refresh_preview](const QString&) { refresh_preview(); });
    refresh_preview();

    auto* buttons = dialog_buttons(
        dialog, QCoreApplication::translate("Afs.AfsEditUi", "Export"));
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return std::optional<std::filesystem::path>{};
    }

    const auto mode = static_cast<cricodecs::afs::AfsHeaderNameMode>(mode_combo->currentData().toInt());
    auto header = afs.build_file_id_header(
        qstring_to_utf8(archive_name_edit->text().trimmed()),
        qstring_to_utf8(prefix_edit->text()),
        mode
    );
    if (!header) {
        return std::unexpected(QCoreApplication::translate("Afs.AfsEditUi", "AFS header export failed: %1").arg(utf8_to_qstring(header.error())));
    }

    const auto default_name = safe_output_name(QFileInfo(archive_name_edit->text().trimmed()).completeBaseName(), QStringLiteral(".h"));
    const auto path_text = QFileDialog::getSaveFileName(
        parent,
        QCoreApplication::translate("Afs.AfsEditUi", "Export AFS file-ID header"),
        default_name,
        QCoreApplication::translate("Afs.AfsEditUi", "C/C++ headers (*.h);;All files (*)")
    );
    if (path_text.isEmpty()) {
        return std::optional<std::filesystem::path>{};
    }

    const std::vector<uint8_t> bytes(header->begin(), header->end());
    auto result = write_binary_file(path_from_qstring(path_text), bytes);
    if (!result) {
        return std::unexpected(QCoreApplication::translate("Afs.AfsEditUi", "AFS header export failed: %1").arg(utf8_to_qstring(result.error())));
    }
    return path_from_qstring(path_text);
}

} // namespace cristudio::modules::afs
