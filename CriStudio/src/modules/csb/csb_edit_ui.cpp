#include "modules/csb/csb_edit_ui.hpp"

#include "editor/editor_helpers.hpp"
#include "editor/editor_widgets.hpp"
#include "path_text.hpp"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace cristudio::modules::csb {
namespace {

} // namespace

std::expected<std::optional<DirectoryBuildConfig>, QString> choose_directory_build_config(
    QWidget* parent,
    QString title
) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("Csb.CsbEditUi", "CSB Folder Builder"));
    dialog.setMinimumWidth(540);

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    layout->addLayout(form);

    auto* target_label = value_label(QCoreApplication::translate("Csb.CsbEditUi", "CSB directory rebuild"), &dialog);
    target_label->setMinimumWidth(200);
    form->addRow(QCoreApplication::translate("Csb.CsbEditUi", "Target"), target_label);

    auto* source_edit = new QLineEdit(&dialog);
    form->addRow(QCoreApplication::translate("Csb.CsbEditUi", "Source folder"), path_picker_row(
        dialog, *source_edit, QCoreApplication::translate("Csb.CsbEditUi", "Browse"),
        QCoreApplication::translate("Csb.CsbEditUi", "Choose extracted CSB source folder"), PathPickerMode::Directory));

    auto* output_edit = new QLineEdit(&dialog);
    output_edit->setText(safe_output_name(build_output_base_name(std::move(title)), QStringLiteral(".csb")));
    form->addRow(QCoreApplication::translate("Csb.CsbEditUi", "Output"), path_picker_row(
        dialog, *output_edit, QCoreApplication::translate("Csb.CsbEditUi", "Browse"),
        QCoreApplication::translate("Csb.CsbEditUi", "Choose CSB build output"), PathPickerMode::SaveFile,
        QCoreApplication::translate("Csb.CsbEditUi", "CRI CSB (*.csb);;All files (*)")));

    auto* details = dim_label(
        QCoreApplication::translate("Csb.CsbEditUi", "The native builder recurses through the folder, uses relative paths as stream names, and supports the payload types accepted by CsbContainer."),
        &dialog
    );
    details->setWordWrap(true);
    form->addRow(QString{}, details);

    auto* buttons = dialog_buttons(
        dialog, QCoreApplication::translate("Csb.CsbEditUi", "Build"));
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::optional<DirectoryBuildConfig>{};
    }

    DirectoryBuildConfig config;
    config.input_dir = path_from_qstring(source_edit->text().trimmed());
    config.output_path = path_from_qstring(output_edit->text().trimmed());
    if (config.input_dir.empty()) {
        return std::unexpected(QCoreApplication::translate("Csb.CsbEditUi", "Choose a CSB source folder."));
    }
    if (config.output_path.empty()) {
        return std::unexpected(QCoreApplication::translate("Csb.CsbEditUi", "Choose an output path."));
    }
    return std::optional<DirectoryBuildConfig>(std::move(config));
}

} // namespace cristudio::modules::csb
