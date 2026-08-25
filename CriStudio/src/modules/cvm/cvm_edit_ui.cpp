#include "modules/cvm/cvm_edit_ui.hpp"

#include "editor/editor_helpers.hpp"
#include "editor/editor_widgets.hpp"
#include "editor/table_item_helpers.hpp"
#include "path_text.hpp"

#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <string>
#include <utility>

namespace cristudio::modules::cvm {
namespace {

struct ParsedRecordingDate {
    QDateTime date_time;
    int gmt_offset = 0;
};

std::optional<ParsedRecordingDate> parse_recording_date(QString text) {
    text = text.trimmed();
    static const QRegularExpression extended(
        QStringLiteral(R"(^(\d{2})/(\d{2})/(\d{4}) (\d{2}):(\d{2}):(\d{2}):\d{2}:([+-]?\d+)$)"));
    const auto match = extended.match(text);
    if (match.hasMatch()) {
        const QDate date(match.captured(3).toInt(), match.captured(2).toInt(), match.captured(1).toInt());
        const QTime time(match.captured(4).toInt(), match.captured(5).toInt(), match.captured(6).toInt());
        if (date.isValid() && time.isValid()) {
            return ParsedRecordingDate{QDateTime(date, time), match.captured(7).toInt()};
        }
    }
    const auto date_time = QDateTime::fromString(text, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (date_time.isValid()) {
        return ParsedRecordingDate{date_time, 0};
    }
    return std::nullopt;
}

} // namespace

void populate_editor_archive_table(QTableWidget* table, const cricodecs::cvm::CvmContainer& cvm) {
    table->clear();
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({
        QCoreApplication::translate("Cvm.CvmEditUi", "Index"),
        QCoreApplication::translate("Cvm.CvmEditUi", "Archive Path"),
        QCoreApplication::translate("Cvm.CvmEditUi", "Extent Sector"),
        QCoreApplication::translate("Cvm.CvmEditUi", "Size")
    });
    table->setRowCount(static_cast<int>(cvm.entry_count()));
    for (const auto& entry : cvm.entries()) {
        const auto row = static_cast<int>(entry.index);
        set_table_item(table, row, 0, QString::number(entry.index), false);
        set_table_item(table, row, 1, path_to_qstring(entry.path), true);
        set_table_item(table, row, 2, QString::number(entry.extent_sector), false);
        set_table_item(table, row, 3, QString::number(entry.size), false);
    }
}

std::optional<std::filesystem::path> choose_entry_path(QWidget* parent, const cricodecs::cvm::CvmEntry& entry) {
    bool ok = false;
    const auto path = QInputDialog::getText(
        parent,
        QCoreApplication::translate("Cvm.CvmEditUi", "CVM entry path"),
        QCoreApplication::translate("Cvm.CvmEditUi", "ROFS path"),
        QLineEdit::Normal,
        path_to_qstring(entry.path),
        &ok
    );
    if (!ok || path.trimmed().isEmpty()) {
        return std::nullopt;
    }
    return path_from_qstring(path.trimmed());
}

std::optional<MetadataOptions> choose_metadata_options(QWidget* parent, const cricodecs::cvm::CvmContainer& cvm) {
    const auto& primary = cvm.primary_volume();
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Cvm.CvmEditUi", "CVM/ROFS metadata"));
    dialog.setMinimumWidth(520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* disc_edit = new QLineEdit(utf8_to_qstring(cvm.disc_name()), &dialog);
    const auto source_recording_date = utf8_to_qstring(cvm.recording_date_text());
    const auto parsed_recording_date = parse_recording_date(source_recording_date);
    auto* recording_date_mode = new QComboBox(&dialog);
    recording_date_mode->addItem(QCoreApplication::translate("Cvm.CvmEditUi", "Date and time"), 0);
    recording_date_mode->addItem(QCoreApplication::translate("Cvm.CvmEditUi", "Raw source text"), 1);
    auto* recording_date_enabled = new QCheckBox(QCoreApplication::translate("Cvm.CvmEditUi", "Write recording date"), &dialog);
    recording_date_enabled->setChecked(!source_recording_date.trimmed().isEmpty());
    auto* recording_date_edit = new QDateTimeEdit(
        parsed_recording_date ? parsed_recording_date->date_time : QDateTime::currentDateTime(), &dialog);
    recording_date_edit->setCalendarPopup(true);
    recording_date_edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    recording_date_edit->setMinimumDateTime(QDateTime(QDate(1900, 1, 1), QTime(0, 0)));
    recording_date_edit->setMaximumDateTime(QDateTime(QDate(2155, 12, 31), QTime(23, 59, 59)));
    auto* gmt_offset_spin = new QSpinBox(&dialog);
    gmt_offset_spin->setRange(-48, 52);
    gmt_offset_spin->setValue(parsed_recording_date ? parsed_recording_date->gmt_offset : 0);
    gmt_offset_spin->setSuffix(QCoreApplication::translate("Cvm.CvmEditUi", " × 15 min"));
    auto* raw_recording_date_edit = new QLineEdit(source_recording_date, &dialog);
    raw_recording_date_edit->setPlaceholderText(QCoreApplication::translate("Cvm.CvmEditUi", "DD/MM/YYYY HH:MM:SS:FF:TZ"));
    if (!source_recording_date.isEmpty() && !parsed_recording_date) {
        recording_date_mode->setCurrentIndex(1);
    }
    const auto sync_recording_date_controls = [=] {
        const bool enabled = recording_date_enabled->isChecked();
        const bool raw = recording_date_mode->currentData().toInt() == 1;
        recording_date_mode->setEnabled(enabled);
        recording_date_edit->setVisible(enabled && !raw);
        gmt_offset_spin->setVisible(enabled && !raw);
        raw_recording_date_edit->setVisible(enabled && raw);
        if (auto* label = form->labelForField(recording_date_edit); label != nullptr) {
            label->setVisible(enabled && !raw);
        }
        if (auto* label = form->labelForField(gmt_offset_spin); label != nullptr) {
            label->setVisible(enabled && !raw);
        }
        if (auto* label = form->labelForField(raw_recording_date_edit); label != nullptr) {
            label->setVisible(enabled && raw);
        }
    };
    QObject::connect(recording_date_enabled, &QCheckBox::toggled, &dialog, [sync_recording_date_controls](bool) {
        sync_recording_date_controls();
    });
    QObject::connect(recording_date_mode, &QComboBox::currentIndexChanged, &dialog, [sync_recording_date_controls](int) {
        sync_recording_date_controls();
    });
    sync_recording_date_controls();
    auto* media_combo = new QComboBox(&dialog);
    media_combo->addItem(QStringLiteral("DVD"), QStringLiteral("DVD"));
    media_combo->addItem(QStringLiteral("CD"), QStringLiteral("CD"));
    if (const int index = media_combo->findData(utf8_to_qstring(cvm.media())); index >= 0) {
        media_combo->setCurrentIndex(index);
    }
    auto* system_edit = new QLineEdit(utf8_to_qstring(primary.system_identifier), &dialog);
    auto* volume_edit = new QLineEdit(utf8_to_qstring(primary.volume_identifier), &dialog);
    auto* volume_set_edit = new QLineEdit(utf8_to_qstring(primary.volume_set_identifier), &dialog);
    auto* publisher_edit = new QLineEdit(utf8_to_qstring(primary.publisher_identifier), &dialog);
    auto* preparer_edit = new QLineEdit(utf8_to_qstring(primary.data_preparer_identifier), &dialog);
    auto* application_edit = new QLineEdit(utf8_to_qstring(primary.application_identifier), &dialog);

    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "Disc name"), disc_edit);
    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "Recording date"), recording_date_enabled);
    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "Date input"), recording_date_mode);
    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "Date and time"), recording_date_edit);
    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "GMT offset"), gmt_offset_spin);
    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "Raw date"), raw_recording_date_edit);
    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "Media"), media_combo);
    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "System identifier"), system_edit);
    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "Volume identifier"), volume_edit);
    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "Volume set"), volume_set_edit);
    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "Publisher"), publisher_edit);
    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "Data preparer"), preparer_edit);
    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "Application"), application_edit);
    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "Logical block size"), new QLabel(QString::number(primary.logical_block_size), &dialog));
    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "Volume space size"), new QLabel(QString::number(primary.volume_space_size), &dialog));
    form->addRow(QCoreApplication::translate("Cvm.CvmEditUi", "Embedded ISO sectors"), new QLabel(QString::number(cvm.embedded_iso_sector_count()), &dialog));
    sync_recording_date_controls();
    layout->addLayout(form);

    auto* note = dim_label(
        QCoreApplication::translate("Cvm.CvmEditUi", "These fields are the public mutable CVM/ROFS metadata fields. Derived sizes and layout sectors are rebuilt by the native builder."),
        &dialog
    );
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = dialog_buttons(dialog);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    QString recording_date;
    if (recording_date_enabled->isChecked()) {
        if (recording_date_mode->currentData().toInt() == 1) {
            recording_date = raw_recording_date_edit->text().trimmed();
        } else {
            recording_date = recording_date_edit->dateTime().toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")) +
                QStringLiteral(":00:%1").arg(gmt_offset_spin->value());
        }
    }
    return MetadataOptions{
        .disc_name = qstring_to_utf8(disc_edit->text()),
        .recording_date = qstring_to_utf8(recording_date),
        .media = qstring_to_utf8(media_combo->currentData().toString()),
        .system_identifier = qstring_to_utf8(system_edit->text()),
        .volume_identifier = qstring_to_utf8(volume_edit->text()),
        .volume_set_identifier = qstring_to_utf8(volume_set_edit->text()),
        .publisher_identifier = qstring_to_utf8(publisher_edit->text()),
        .data_preparer_identifier = qstring_to_utf8(preparer_edit->text()),
        .application_identifier = qstring_to_utf8(application_edit->text())
    };
}

std::optional<std::filesystem::path> choose_import_script(QWidget* parent) {
    const auto path_text = QFileDialog::getOpenFileName(
        parent,
        QCoreApplication::translate("Cvm.CvmEditUi", "Import CVM build script"),
        QString{},
        QCoreApplication::translate("Cvm.CvmEditUi", "CVM scripts (*.cvs);;All files (*)")
    );
    if (path_text.isEmpty()) {
        return std::nullopt;
    }
    return path_from_qstring(path_text);
}

std::optional<std::filesystem::path> choose_export_script(QWidget* parent, QString title) {
    const auto default_name = safe_output_name(
        title.isEmpty() ? QStringLiteral("cvm-session") : title,
        QStringLiteral(".cvs")
    );
    const auto path_text = QFileDialog::getSaveFileName(
        parent,
        QCoreApplication::translate("Cvm.CvmEditUi", "Export CVM build script"),
        default_name,
        QCoreApplication::translate("Cvm.CvmEditUi", "CVM scripts (*.cvs);;All files (*)")
    );
    if (path_text.isEmpty()) {
        return std::nullopt;
    }
    return path_from_qstring(path_text);
}

} // namespace cristudio::modules::cvm
