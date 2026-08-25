#include "shared/i18n.hpp"
#include "editor/editor_document_ui.hpp"

#include "editor/archive_editor_helpers.hpp"
#include "editor/editor_helpers.hpp"
#include "editor/hex_preview_widget.hpp"
#include "editor/editor_widgets.hpp"
#include "editor/transform_editor_helpers.hpp"
#include "editor/transform_detail_model.hpp"
#include "editor_workspace.hpp"
#include "entry_table_model.hpp"
#include "modules/acb/acb_edit.hpp"
#include "modules/acx/acx_edit_ui.hpp"
#include "modules/acx/acx_edit.hpp"
#include "modules/afs/afs_edit_ui.hpp"
#include "modules/afs/afs_edit.hpp"
#include "modules/awb/awb_edit_ui.hpp"
#include "modules/awb/awb_edit.hpp"
#include "modules/cpk/cpk_edit.hpp"
#include "modules/cpk/cpk_edit_ui.hpp"
#include "modules/cvm/cvm_edit.hpp"
#include "modules/cvm/cvm_edit_ui.hpp"
#include "path_text.hpp"
#include "acb_container.hpp"
#include "utf_table.hpp"

#include <QCoreApplication>
#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleValidator>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableView>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <vector>

namespace cristudio {
namespace {

class EditorTableWidget final : public QTableWidget {
public:
    explicit EditorTableWidget(QWidget* parent = nullptr)
        : QTableWidget(parent) {}

    void set_preserve_horizontal_selection(bool enabled) {
        m_preserve_horizontal_selection = enabled;
    }

protected:
    void scrollTo(const QModelIndex& index, ScrollHint hint) override {
        if (!m_preserve_horizontal_selection || horizontalScrollBar() == nullptr) {
            QTableWidget::scrollTo(index, hint);
            return;
        }
        const int previous = horizontalScrollBar()->value();
        QTableWidget::scrollTo(index, hint);
        horizontalScrollBar()->setValue(previous);
    }

private:
    bool m_preserve_horizontal_selection = false;
};

class EditorTableView final : public QTableView {
public:
    explicit EditorTableView(QWidget* parent = nullptr)
        : QTableView(parent) {}

protected:
    void scrollTo(const QModelIndex& index, ScrollHint hint) override {
        if (horizontalScrollBar() == nullptr) {
            QTableView::scrollTo(index, hint);
            return;
        }
        const int previous = horizontalScrollBar()->value();
        QTableView::scrollTo(index, hint);
        horizontalScrollBar()->setValue(previous);
    }
};

QPushButton* toolbar_button(QString text, QWidget* parent) {
    return new QPushButton(std::move(text), parent);
}

QTableWidget* editor_table(QWidget* parent, QAbstractItemView::SelectionBehavior selection, QAbstractItemView::EditTriggers edits) {
    auto* table = new EditorTableWidget(parent);
    table->setAlternatingRowColors(true);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(selection);
    table->setEditTriggers(edits);
    table->setTextElideMode(Qt::ElideRight);
    table->setWordWrap(false);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setDefaultSectionSize(28);
    return table;
}

QTableView* transform_table(QWidget* parent) {
    auto* table = new EditorTableView(parent);
    table->setAlternatingRowColors(true);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setTextElideMode(Qt::ElideRight);
    table->setWordWrap(false);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setDefaultSectionSize(28);
    table->setColumnWidth(0, 260);
    return table;
}

bool matches_transform_filter(const modules::TransformDetailRow& detail, QStringView filter_text) {
    if (filter_text.isEmpty()) {
        return true;
    }
    return detail.field.contains(filter_text, Qt::CaseInsensitive) ||
        detail.value.contains(filter_text, Qt::CaseInsensitive);
}

void append_info_rows(std::vector<InfoRow>& rows, const std::vector<modules::TransformDetailRow>& details, size_t max_rows = 16) {
    size_t added = 0;
    size_t omitted = 0;
    for (const auto& detail : details) {
        if (detail.payload_kind != 0) {
            continue;
        }
        if (added >= max_rows) {
            ++omitted;
            continue;
        }
        rows.push_back({qstring_to_utf8(detail.field), qstring_to_utf8(detail.value)});
        ++added;
    }
    (void)omitted;
}

std::vector<InfoRow> document_info_rows(const EditorDocumentInfoView& view) {
    std::vector<InfoRow> rows;
    if (view.request == nullptr) {
        return rows;
    }

    rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Session"), view.request->source_kind == EditorOpenRequest::SourceKind::Scratch ? cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Scratch") : cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Independent copy")});
    rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Format"), editor_format_label(*view.request).toStdString()});
    rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Bytes"), std::to_string(view.byte_count)});
    if (!view.request->source_path.empty()) {
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Source path"), path_to_utf8(view.request->source_path)});
    }
    if (!view.request->source_archive_path.empty()) {
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Source archive"), path_to_utf8(view.request->source_archive_path)});
    }
    if (!view.request->source_archive_format.empty()) {
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Archive format"), view.request->source_archive_format});
    }
    rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Validation"), view.utf != nullptr ? cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "UTF native build path available") : cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Inspection/Save As copy path")});

    if (view.utf != nullptr) {
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Table"), std::string(view.utf->table_name())});
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Version"), std::to_string(view.utf->version())});
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Rows"), std::to_string(view.utf->row_count())});
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Columns"), std::to_string(view.utf->column_count())});
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Row width"), std::to_string(view.utf->row_width())});
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Data alignment"), std::to_string(view.utf->data_alignment())});
        const auto table_size = view.utf->table_size() != 0 ? view.utf->table_size() : static_cast<uint32_t>(view.byte_count);
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Table size"), std::to_string(table_size)});
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Text encoding"), view.utf->text_encoding() ? *view.utf->text_encoding() : cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "default")});
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Serialized bytes"), std::to_string(view.byte_count)});
    } else if (view.transform_kind != TransformKind::None && view.transform != nullptr) {
        append_transform_info_rows(rows, view.transform_kind, *view.transform);
    } else if (view.archive != nullptr && view.archive->kind == ArchiveKind::Afs && view.archive->afs != nullptr) {
        rows.back().value = cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "AFS native archive build path available");
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Archive kind"), "AFS"});
        append_info_rows(rows, modules::afs::detail_rows(*view.archive->afs));
    } else if (view.archive != nullptr && view.archive->kind == ArchiveKind::Awb && view.archive->awb != nullptr) {
        rows.back().value = cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "AWB native archive build path available");
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Archive kind"), cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "AWB/AFS2")});
        append_info_rows(rows, modules::awb::detail_rows(*view.archive->awb, view.request->keys));
    } else if (view.archive != nullptr && view.archive->kind == ArchiveKind::Acx && view.archive->acx != nullptr) {
        rows.back().value = cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "ACX native rebuild path available");
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Archive kind"), "ACX"});
        append_info_rows(rows, modules::acx::detail_rows(*view.archive->acx));
    } else if (view.archive != nullptr && view.archive->kind == ArchiveKind::Cpk && view.archive->cpk != nullptr) {
        rows.back().value = cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "CPK native archive save path available");
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Archive kind"), "CPK"});
        append_info_rows(rows, modules::cpk::detail_rows(*view.archive->cpk), 32);
    } else if (view.archive != nullptr && view.archive->kind == ArchiveKind::Cvm && view.archive->cvm != nullptr) {
        rows.back().value = cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "CVM native ROFS save path available");
        rows.push_back({cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Archive kind"), cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "CVM/ROFS")});
        append_info_rows(rows, modules::cvm::detail_rows(*view.archive->cvm), 32);
    }

    return rows;
}

} // namespace

EditorDocumentUi build_editor_document_ui(QWidget* parent) {
    EditorDocumentUi ui;

    auto* outer = new QVBoxLayout(parent);
    outer->setContentsMargins(8, 2, 8, 8);
    outer->setSpacing(6);

    auto* header = new QWidget(parent);
    auto* header_layout = new QGridLayout(header);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setHorizontalSpacing(16);
    header_layout->setVerticalSpacing(2);
    ui.title_label = new QLabel(header);
    ui.title_label->setObjectName(QStringLiteral("DocumentTitle"));
    ui.subtitle_label = new QLabel(header);
    ui.subtitle_label->setObjectName(QStringLiteral("DocumentSubtitle"));
    header_layout->addWidget(ui.title_label, 0, 0);
    header_layout->addWidget(ui.subtitle_label, 1, 0);

    auto* info_content = new QWidget(header);
    ui.info_grid = new QGridLayout(info_content);
    ui.info_grid->setContentsMargins(0, 0, 0, 0);
    ui.info_grid->setHorizontalSpacing(12);
    ui.info_grid->setVerticalSpacing(2);
    ui.info_grid->setColumnStretch(1, 1);
    ui.info_grid->setColumnStretch(3, 1);
    header_layout->addWidget(info_content, 0, 1, 2, 1, Qt::AlignVCenter);

    auto* action_panel = new QWidget(header);
    auto* action_layout = new QGridLayout(action_panel);
    action_layout->setContentsMargins(0, 0, 0, 0);
    action_layout->setHorizontalSpacing(8);
    action_layout->setVerticalSpacing(6);

    ui.save_button = new QToolButton(action_panel);
    ui.save_button->setObjectName(QStringLiteral("ActionButton"));
    ui.save_button->setText(QCoreApplication::translate("Editor.EditorDocumentUi", "Save"));
    ui.save_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    ui.save_button->setToolTip(QCoreApplication::translate("Editor.EditorDocumentUi", "Save to the last path chosen with Save As; asks for a path the first time."));
    action_layout->addWidget(ui.save_button, 0, 0);

    ui.save_as_button = new QToolButton(action_panel);
    ui.save_as_button->setObjectName(QStringLiteral("ActionButton"));
    ui.save_as_button->setText(QCoreApplication::translate("Editor.EditorDocumentUi", "Save As"));
    ui.save_as_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    ui.save_as_button->setToolTip(QCoreApplication::translate("Editor.EditorDocumentUi", "Build or copy this independent editor session to a chosen path."));
    action_layout->addWidget(ui.save_as_button, 0, 1);

    ui.build_button = new QToolButton(action_panel);
    ui.build_button->setObjectName(QStringLiteral("ActionButton"));
    ui.build_button->setText(QCoreApplication::translate("Editor.EditorDocumentUi", "Build"));
    ui.build_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    ui.build_button->setToolTip(QCoreApplication::translate("Editor.EditorDocumentUi", "Validate and rebuild the in-memory editor object."));
    action_layout->addWidget(ui.build_button, 1, 0);

    ui.extract_button = new QToolButton(action_panel);
    ui.extract_button->setObjectName(QStringLiteral("ActionButton"));
    ui.extract_button->setText(QCoreApplication::translate("Editor.EditorDocumentUi", "Extract"));
    ui.extract_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    ui.extract_button->setToolTip(QCoreApplication::translate("Editor.EditorDocumentUi", "Write this independent editor session's current bytes to a chosen folder."));
    action_layout->addWidget(ui.extract_button, 1, 1);
    header_layout->addWidget(action_panel, 0, 2, 2, 1, Qt::AlignRight | Qt::AlignVCenter);
    header_layout->setColumnStretch(1, 1);
    outer->addWidget(header);

    ui.utf_toolbar = new QWidget(parent);
    auto* utf_toolbar_layout = new QHBoxLayout(ui.utf_toolbar);
    utf_toolbar_layout->setContentsMargins(0, 0, 0, 0);
    utf_toolbar_layout->setSpacing(8);
    ui.table_label = dim_label(QCoreApplication::translate("Editor.EditorDocumentUi", "Table"), ui.utf_toolbar);
    utf_toolbar_layout->addWidget(ui.table_label, 0);
    ui.table_name_edit = new QLineEdit(ui.utf_toolbar);
    ui.table_name_edit->setClearButtonEnabled(true);
    ui.table_name_edit->setToolTip(QCoreApplication::translate("Editor.EditorDocumentUi", "Rename the UTF table."));
    utf_toolbar_layout->addWidget(ui.table_name_edit, 2);
    ui.apply_table_name_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Rename Table"), ui.utf_toolbar);
    ui.add_row_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Add Row"), ui.utf_toolbar);
    ui.remove_row_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Remove Row"), ui.utf_toolbar);
    ui.add_column_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Add Column"), ui.utf_toolbar);
    ui.remove_column_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Remove Column"), ui.utf_toolbar);
    utf_toolbar_layout->addWidget(ui.apply_table_name_button, 0);
    utf_toolbar_layout->addWidget(ui.add_row_button, 0);
    utf_toolbar_layout->addWidget(ui.remove_row_button, 0);
    utf_toolbar_layout->addWidget(ui.add_column_button, 0);
    utf_toolbar_layout->addWidget(ui.remove_column_button, 0);
    utf_toolbar_layout->addStretch(1);
    ui.utf_toolbar->hide();
    outer->addWidget(ui.utf_toolbar);

    ui.archive_toolbar = new QWidget(parent);
    auto* archive_toolbar_layout = new QHBoxLayout(ui.archive_toolbar);
    archive_toolbar_layout->setContentsMargins(0, 0, 0, 0);
    archive_toolbar_layout->setSpacing(8);
    ui.archive_kind_label = dim_label(QCoreApplication::translate("Editor.EditorDocumentUi", "Archive"), ui.archive_toolbar);
    archive_toolbar_layout->addWidget(ui.archive_kind_label, 0);
    ui.add_archive_file_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Add File"), ui.archive_toolbar);
    ui.replace_archive_file_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Replace File"), ui.archive_toolbar);
    ui.remove_archive_file_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Remove Entry"), ui.archive_toolbar);
    ui.move_archive_entry_up_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Move Up"), ui.archive_toolbar);
    ui.move_archive_entry_down_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Move Down"), ui.archive_toolbar);
    ui.rename_archive_entry_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Rename Entry"), ui.archive_toolbar);
    ui.reserve_afs_id_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Reserve ID"), ui.archive_toolbar);
    ui.set_afs_timestamp_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Set Timestamp"), ui.archive_toolbar);
    ui.set_archive_wave_id_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Set Wave ID"), ui.archive_toolbar);
    ui.batch_awb_wave_ids_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Batch Wave IDs"), ui.archive_toolbar);
    ui.archive_entry_options_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Entry Props"), ui.archive_toolbar);
    ui.archive_options_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Options"), ui.archive_toolbar);
    ui.archive_compression_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Compression"), ui.archive_toolbar);
    auto* archive_compression_menu = new QMenu(ui.archive_compression_button);
    ui.archive_compress_all_action = archive_compression_menu->addAction(QCoreApplication::translate("Editor.EditorDocumentUi", "Compress all on save"));
    ui.archive_store_all_action = archive_compression_menu->addAction(QCoreApplication::translate("Editor.EditorDocumentUi", "Store all uncompressed"));
    ui.archive_compression_button->setMenu(archive_compression_menu);
    ui.archive_compression_button->setToolTip(QCoreApplication::translate("Editor.EditorDocumentUi", "Set the save-time compression policy for every CPK entry."));
    ui.import_afs_als_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Import ALS"), ui.archive_toolbar);
    ui.export_afs_header_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Export Header"), ui.archive_toolbar);
    ui.import_cvm_script_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Import CVS"), ui.archive_toolbar);
    ui.export_cvm_script_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Export CVS"), ui.archive_toolbar);
    ui.extract_archive_entry_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Extract Entry"), ui.archive_toolbar);
    ui.extract_raw_archive_entry_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Extract Raw"), ui.archive_toolbar);
    for (auto* button : {
        ui.add_archive_file_button, ui.replace_archive_file_button, ui.remove_archive_file_button,
        ui.move_archive_entry_up_button, ui.move_archive_entry_down_button, ui.rename_archive_entry_button,
        ui.reserve_afs_id_button, ui.set_afs_timestamp_button, ui.set_archive_wave_id_button,
        ui.batch_awb_wave_ids_button, ui.archive_entry_options_button, ui.archive_options_button,
        ui.archive_compression_button,
        ui.import_afs_als_button, ui.export_afs_header_button, ui.import_cvm_script_button,
        ui.export_cvm_script_button, ui.extract_archive_entry_button, ui.extract_raw_archive_entry_button
    }) {
        archive_toolbar_layout->addWidget(button, 0);
    }
    archive_toolbar_layout->addStretch(1);
    ui.archive_toolbar->hide();
    outer->addWidget(ui.archive_toolbar);

    ui.transform_toolbar = new QWidget(parent);
    auto* transform_toolbar_layout = new QHBoxLayout(ui.transform_toolbar);
    transform_toolbar_layout->setContentsMargins(0, 0, 0, 0);
    transform_toolbar_layout->setSpacing(8);
    ui.transform_kind_label = dim_label(QCoreApplication::translate("Editor.EditorDocumentUi", "Transform"), ui.transform_toolbar);
    transform_toolbar_layout->addWidget(ui.transform_kind_label, 0);
    ui.encode_transform_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Encode from WAV"), ui.transform_toolbar);
    ui.decode_transform_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Decode WAV"), ui.transform_toolbar);
    ui.decrypt_transform_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Decrypt"), ui.transform_toolbar);
    ui.encrypt_transform_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Encrypt"), ui.transform_toolbar);
    ui.rebuild_transform_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Rebuild"), ui.transform_toolbar);
    ui.transform_options_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Options"), ui.transform_toolbar);
    ui.extract_transform_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Extract"), ui.transform_toolbar);
    ui.adx_container_build_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Build from ADX"), ui.transform_toolbar);
    ui.csb_directory_build_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Build from Folder"), ui.transform_toolbar);
    ui.media_build_wizard_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Build Wizard"), ui.transform_toolbar);
    ui.editor_mux_preview_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Preview"), ui.transform_toolbar);
    ui.open_acb_awb_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Open AWB"), ui.transform_toolbar);
    ui.export_acb_awb_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Export AWB"), ui.transform_toolbar);
    ui.add_transform_entry_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Add"), ui.transform_toolbar);
    ui.replace_transform_entry_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Replace"), ui.transform_toolbar);
    ui.remove_transform_entry_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Remove"), ui.transform_toolbar);
    ui.move_transform_entry_up_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Up"), ui.transform_toolbar);
    ui.move_transform_entry_down_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Down"), ui.transform_toolbar);
    ui.rename_transform_entry_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Rename"), ui.transform_toolbar);
    ui.toggle_transform_entry_flag_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Toggle"), ui.transform_toolbar);
    ui.transform_filter_edit = new QLineEdit(ui.transform_toolbar);
    ui.transform_filter_edit->setClearButtonEnabled(true);
    ui.transform_filter_edit->setPlaceholderText(QCoreApplication::translate("Editor.EditorDocumentUi", "Filter rows"));
    ui.transform_filter_edit->setToolTip(QCoreApplication::translate("Editor.EditorDocumentUi", "Filter transform rows by field or value."));
    for (auto* button : {
        ui.encode_transform_button, ui.decode_transform_button, ui.decrypt_transform_button,
        ui.encrypt_transform_button, ui.rebuild_transform_button, ui.transform_options_button,
        ui.extract_transform_button, ui.adx_container_build_button, ui.csb_directory_build_button,
        ui.media_build_wizard_button, ui.editor_mux_preview_button, ui.open_acb_awb_button, ui.export_acb_awb_button,
        ui.add_transform_entry_button, ui.replace_transform_entry_button, ui.remove_transform_entry_button,
        ui.move_transform_entry_up_button, ui.move_transform_entry_down_button,
        ui.rename_transform_entry_button, ui.toggle_transform_entry_flag_button
    }) {
        transform_toolbar_layout->addWidget(button, 0);
    }
    transform_toolbar_layout->addWidget(ui.transform_filter_edit, 1);
    transform_toolbar_layout->addStretch(1);
    ui.transform_toolbar->hide();
    outer->addWidget(ui.transform_toolbar);

    ui.cri_key_panel = new QWidget(parent);
    auto* key_layout = new QHBoxLayout(ui.cri_key_panel);
    key_layout->setContentsMargins(0, 0, 0, 0);
    key_layout->setSpacing(6);
    ui.local_key_label = dim_label(QCoreApplication::translate("Editor.EditorDocumentUi", "Local CRI key"), ui.cri_key_panel);
    key_layout->addWidget(ui.local_key_label, 0);
    ui.local_key_type = new QComboBox(ui.cri_key_panel);
    ui.local_key_type->addItem(QCoreApplication::translate("Editor.EditorDocumentUi", "No key"), static_cast<int>(DecryptionKeys::AdxMode::None));
    ui.local_key_type->addItem(QCoreApplication::translate("Editor.EditorDocumentUi", "Type 8 string"), static_cast<int>(DecryptionKeys::AdxMode::Type8String));
    ui.local_key_type->addItem(QCoreApplication::translate("Editor.EditorDocumentUi", "Type 9 number"), static_cast<int>(DecryptionKeys::AdxMode::Type9Number));
    ui.local_key_type->addItem(QCoreApplication::translate("Editor.EditorDocumentUi", "Key triplet"), static_cast<int>(DecryptionKeys::AdxMode::AhxTriplet));
    ui.local_key_type->setToolTip(QCoreApplication::translate("Editor.EditorDocumentUi", "Choose the ADX/AHX key representation explicitly."));
    ui.local_key_type->hide();
    key_layout->addWidget(ui.local_key_type, 0);
    ui.cri_key_edit = new QLineEdit(ui.cri_key_panel);
    ui.cri_key_edit->setClearButtonEnabled(true);
    ui.cri_key_edit->setPlaceholderText(QCoreApplication::translate("Editor.EditorDocumentUi", "No key"));
    ui.cri_key_edit->setToolTip(QCoreApplication::translate("Editor.EditorDocumentUi", "This key belongs only to this editor tab and does not change the global CRI key."));
    key_layout->addWidget(ui.cri_key_edit, 1);
    ui.cri_key_base = new QComboBox(ui.cri_key_panel);
    ui.cri_key_base->addItem(QCoreApplication::translate("Editor.EditorDocumentUi", "hex"), 16);
    ui.cri_key_base->addItem(QCoreApplication::translate("Editor.EditorDocumentUi", "dec"), 10);
    key_layout->addWidget(ui.cri_key_base, 0);
    ui.adx_subkey_panel = new QWidget(ui.cri_key_panel);
    auto* adx_subkey_layout = new QHBoxLayout(ui.adx_subkey_panel);
    adx_subkey_layout->setContentsMargins(0, 0, 0, 0);
    adx_subkey_layout->setSpacing(4);
    ui.adx_subkey_label = dim_label(QCoreApplication::translate("Editor.EditorDocumentUi", "Subkey"), ui.adx_subkey_panel);
    adx_subkey_layout->addWidget(ui.adx_subkey_label);
    ui.adx_subkey_spin = new QSpinBox(ui.adx_subkey_panel);
    ui.adx_subkey_spin->setRange(0, std::numeric_limits<uint16_t>::max());
    adx_subkey_layout->addWidget(ui.adx_subkey_spin);
    ui.adx_subkey_panel->hide();
    key_layout->addWidget(ui.adx_subkey_panel, 0);
    ui.adx_triplet_panel = new QWidget(ui.cri_key_panel);
    auto* triplet_layout = new QHBoxLayout(ui.adx_triplet_panel);
    triplet_layout->setContentsMargins(0, 0, 0, 0);
    triplet_layout->setSpacing(4);
    const auto triplet_validator = [](QLineEdit* edit) {
        edit->setMaximumWidth(72);
        edit->setMaxLength(4);
        edit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9A-Fa-f]{1,4}")), edit));
    };
    ui.adx_triplet_start = new QLineEdit(ui.adx_triplet_panel);
    ui.adx_triplet_mult = new QLineEdit(ui.adx_triplet_panel);
    ui.adx_triplet_add = new QLineEdit(ui.adx_triplet_panel);
    triplet_validator(ui.adx_triplet_start);
    triplet_validator(ui.adx_triplet_mult);
    triplet_validator(ui.adx_triplet_add);
    ui.adx_triplet_start_label = dim_label(QCoreApplication::translate("Editor.EditorDocumentUi", "Start"), ui.adx_triplet_panel);
    triplet_layout->addWidget(ui.adx_triplet_start_label);
    triplet_layout->addWidget(ui.adx_triplet_start);
    ui.adx_triplet_mult_label = dim_label(QCoreApplication::translate("Editor.EditorDocumentUi", "Mult"), ui.adx_triplet_panel);
    triplet_layout->addWidget(ui.adx_triplet_mult_label);
    triplet_layout->addWidget(ui.adx_triplet_mult);
    ui.adx_triplet_add_label = dim_label(QCoreApplication::translate("Editor.EditorDocumentUi", "Add"), ui.adx_triplet_panel);
    triplet_layout->addWidget(ui.adx_triplet_add_label);
    triplet_layout->addWidget(ui.adx_triplet_add);
    ui.adx_triplet_panel->hide();
    key_layout->addWidget(ui.adx_triplet_panel, 1);
    ui.cvm_scramble_panel = new QWidget(ui.cri_key_panel);
    auto* cvm_scramble_layout = new QHBoxLayout(ui.cvm_scramble_panel);
    cvm_scramble_layout->setContentsMargins(0, 0, 0, 0);
    cvm_scramble_layout->setSpacing(8);
    ui.cvm_scramble_check = new ToggleSwitch(ui.cvm_scramble_panel);
    ui.cvm_scramble_check->setAccessibleName(QCoreApplication::translate("Editor.EditorDocumentUi", "Scramble CVM metadata on save"));
    ui.cvm_scramble_check->setToolTip(QCoreApplication::translate("Editor.EditorDocumentUi", "Write a scrambled CVM TOC using this tab's key string. Reading remains automatic."));
    cvm_scramble_layout->addWidget(ui.cvm_scramble_check, 0);
    ui.cvm_scramble_label = dim_label(QCoreApplication::translate("Editor.EditorDocumentUi", "Scramble on save"), ui.cvm_scramble_panel);
    cvm_scramble_layout->addWidget(ui.cvm_scramble_label, 0);
    ui.cvm_scramble_panel->hide();
    key_layout->addWidget(ui.cvm_scramble_panel, 0);
    ui.apply_cri_key_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Apply locally"), ui.cri_key_panel);
    key_layout->addWidget(ui.apply_cri_key_button, 0);
    key_layout->addStretch(1);
    ui.cri_key_panel->hide();
    outer->addWidget(ui.cri_key_panel);

    auto* body = new QSplitter(Qt::Horizontal, parent);
    body->setHandleWidth(13);
    auto* data_stack = new QWidget(body);
    auto* data_stack_layout = new QVBoxLayout(data_stack);
    data_stack_layout->setContentsMargins(0, 0, 0, 0);
    data_stack_layout->setSpacing(0);

    ui.table_model = new EntryTableModel(data_stack);
    ui.table = new QTreeView(data_stack);
    ui.table->setModel(ui.table_model);
    ui.table->setAlternatingRowColors(true);
    ui.table->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.table->setSelectionMode(QAbstractItemView::SingleSelection);
    ui.table->setUniformRowHeights(true);
    ui.table->setRootIsDecorated(true);
    ui.table->setSortingEnabled(true);
    ui.table->header()->setStretchLastSection(false);
    ui.table->header()->setSectionResizeMode(QHeaderView::Interactive);
    ui.table->header()->setMinimumSectionSize(64);
    data_stack_layout->addWidget(ui.table);

    ui.utf_grid = editor_table(
        data_stack,
        QAbstractItemView::SelectItems,
        QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed
    );
    ui.utf_grid->hide();
    data_stack_layout->addWidget(ui.utf_grid);

    ui.archive_table = editor_table(
        data_stack,
        QAbstractItemView::SelectRows,
        QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed
    );
    ui.archive_table->horizontalHeader()->setResizeContentsPrecision(100);
    modules::cpk::configure_editor_archive_table(ui.archive_table);
    ui.archive_table->hide();
    data_stack_layout->addWidget(ui.archive_table);

    ui.transform_model = new TransformDetailModel(data_stack);
    ui.transform_table = transform_table(data_stack);
    ui.transform_table->setModel(ui.transform_model);
    ui.transform_table->hide();
    data_stack_layout->addWidget(ui.transform_table);
    body->addWidget(data_stack);

    auto* inspector = new QWidget(body);
    auto* inspector_layout = new QVBoxLayout(inspector);
    inspector_layout->setContentsMargins(8, 0, 0, 0);
    inspector_layout->setSpacing(8);

    ui.log_toggle_button = new QToolButton(inspector);
    ui.log_toggle_button->setText(QCoreApplication::translate("Editor.EditorDocumentUi", "Log"));
    ui.log_toggle_button->setCheckable(true);
    ui.log_toggle_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    ui.log_toggle_button->setToolTip(QCoreApplication::translate("Editor.EditorDocumentUi", "Show or hide editor session log."));
    inspector_layout->addWidget(ui.log_toggle_button, 0, Qt::AlignRight);

    auto* inspector_splitter = new QSplitter(Qt::Vertical, inspector);
    inspector_splitter->setHandleWidth(9);
    ui.preview_tabs = new QTabWidget(inspector_splitter);
    ui.preview_tabs->setObjectName(QStringLiteral("EditorPreviewTabs"));
    ui.preview_tabs->setDocumentMode(true);
    auto* preview_page = new QWidget(ui.preview_tabs);
    auto* preview_layout = new QVBoxLayout(preview_page);
    preview_layout->setContentsMargins(0, 0, 0, 0);
    preview_layout->setSpacing(6);

    ui.mux_preview_panel = new QWidget(preview_page);
    auto* mux_preview_layout = new QVBoxLayout(ui.mux_preview_panel);
    mux_preview_layout->setContentsMargins(6, 6, 6, 6);
    mux_preview_layout->setSpacing(8);

    ui.video = make_video_display(ui.mux_preview_panel);
    mux_preview_layout->addWidget(ui.video.frame, 8);

    ui.media = make_media_controls(ui.mux_preview_panel);
    mux_preview_layout->addWidget(ui.media.panel, 0, Qt::AlignTop);
    ui.mux_preview_panel->hide();
    preview_layout->addWidget(ui.mux_preview_panel, 8);

    ui.field_model = new EntryTableModel(preview_page);
    ui.field_table = new QTreeView(preview_page);
    ui.field_table->setModel(ui.field_model);
    ui.field_table->setAlternatingRowColors(true);
    ui.field_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.field_table->setSelectionMode(QAbstractItemView::SingleSelection);
    ui.field_table->setRootIsDecorated(false);
    ui.field_table->setUniformRowHeights(true);
    ui.field_table->header()->setStretchLastSection(false);
    ui.field_table->hide();
    preview_layout->addWidget(ui.field_table, 4);

    ui.schema_table = editor_table(
        preview_page,
        QAbstractItemView::SelectRows,
        QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed
    );
    ui.schema_table->setColumnCount(7);
    ui.schema_table->setHorizontalHeaderLabels({
        QCoreApplication::translate("Editor.EditorDocumentUi", "Column"),
        QCoreApplication::translate("Editor.EditorDocumentUi", "Type"),
        QCoreApplication::translate("Editor.EditorDocumentUi", "Flags"),
        QCoreApplication::translate("Editor.EditorDocumentUi", "Default"),
        QCoreApplication::translate("Editor.EditorDocumentUi", "Default Offset"),
        QCoreApplication::translate("Editor.EditorDocumentUi", "Row Offset"),
        QCoreApplication::translate("Editor.EditorDocumentUi", "Index")
    });
    ui.schema_table->setMinimumHeight(132);
    ui.schema_table->hide();
    preview_layout->addWidget(ui.schema_table, 3);

    ui.payload_table = editor_table(preview_page, QAbstractItemView::SelectItems, QAbstractItemView::NoEditTriggers);
    ui.payload_table->hide();
    preview_layout->addWidget(ui.payload_table, 5);

    auto* raw_page = new QWidget(ui.preview_tabs);
    auto* raw_layout = new QVBoxLayout(raw_page);
    raw_layout->setContentsMargins(0, 0, 0, 0);
    raw_layout->setSpacing(0);
    ui.hex_preview = new HexPreviewWidget(raw_page);
    ui.hex_preview->hide();
    raw_layout->addWidget(ui.hex_preview, 1);

    ui.utf_edit_panel = new QWidget(preview_page);
    auto* edit_layout = new QHBoxLayout(ui.utf_edit_panel);
    edit_layout->setContentsMargins(0, 0, 0, 0);
    edit_layout->setSpacing(6);
    ui.value_label = dim_label(QCoreApplication::translate("Editor.EditorDocumentUi", "Value"), ui.utf_edit_panel);
    edit_layout->addWidget(ui.value_label, 0);
    ui.value_type_label = dim_label(QString{}, ui.utf_edit_panel);
    ui.value_type_label->setMinimumWidth(42);
    edit_layout->addWidget(ui.value_type_label, 0);
    ui.value_edit = new QLineEdit(ui.utf_edit_panel);
    ui.value_edit->setClearButtonEnabled(true);
    ui.value_edit->setToolTip(QCoreApplication::translate("Editor.EditorDocumentUi", "Edit the selected UTF cell. Binary and GUID values use hex byte text."));
    ui.unsigned_value_validator = new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral(R"((?:0[xX][0-9A-Fa-f]+|[0-9]+))")), ui.value_edit);
    ui.signed_value_validator = new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral(R"(-?(?:0[xX][0-9A-Fa-f]+|[0-9]+))")), ui.value_edit);
    auto* real_validator = new QDoubleValidator(ui.value_edit);
    real_validator->setNotation(QDoubleValidator::ScientificNotation);
    ui.real_value_validator = real_validator;
    ui.guid_value_validator = new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9A-Fa-f]{32}")), ui.value_edit);
    edit_layout->addWidget(ui.value_edit, 1);
    ui.apply_value_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Apply Value"), ui.utf_edit_panel);
    ui.apply_value_button->setEnabled(false);
    edit_layout->addWidget(ui.apply_value_button, 0);
    ui.rename_column_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Rename Column"), ui.utf_edit_panel);
    ui.rename_column_button->setEnabled(false);
    edit_layout->addWidget(ui.rename_column_button, 0);
    preview_layout->addWidget(ui.utf_edit_panel, 0);

    ui.binary_actions_panel = new QWidget(preview_page);
    auto* binary_layout = new QHBoxLayout(ui.binary_actions_panel);
    binary_layout->setContentsMargins(0, 0, 0, 0);
    ui.replace_binary_button = toolbar_button(QCoreApplication::translate("Editor.EditorDocumentUi", "Replace Binary From File"), ui.binary_actions_panel);
    ui.replace_binary_button->setEnabled(false);
    ui.replace_binary_button->setToolTip(QCoreApplication::translate("Editor.EditorDocumentUi", "For UTF VLData cells, replace this editor session's binary value without mutating the browser-loaded object."));
    binary_layout->addWidget(ui.replace_binary_button, 0);
    binary_layout->addStretch(1);
    preview_layout->addWidget(ui.binary_actions_panel, 0);

    ui.preview_tabs->addTab(preview_page, QCoreApplication::translate("Editor.EditorDocumentUi", "Preview"));
    ui.preview_tabs->addTab(raw_page, QCoreApplication::translate("Editor.EditorDocumentUi", "Raw"));
    ui.preview_tabs->setCurrentIndex(0);
    ui.preview_tabs->setTabEnabled(0, false);
    ui.preview_tabs->setTabEnabled(1, false);
    inspector_splitter->addWidget(ui.preview_tabs);

    ui.log = new QPlainTextEdit(inspector_splitter);
    ui.log->setReadOnly(true);
    ui.log->setMinimumHeight(80);
    ui.log->hide();
    inspector_splitter->addWidget(ui.log);
    inspector_splitter->setSizes({520, 120});
    inspector_layout->addWidget(inspector_splitter, 1);
    QObject::connect(ui.log_toggle_button, &QToolButton::toggled, ui.log, &QPlainTextEdit::setVisible);

    body->addWidget(inspector);
    body->setSizes({980, 420});
    outer->addWidget(body, 1);

    ui.progress = new QProgressBar(parent);
    ui.progress->setRange(0, 0);
    ui.progress->hide();
    outer->addWidget(ui.progress);

    retranslate_editor_document_ui(ui);
    return ui;
}

void retranslate_editor_document_ui(EditorDocumentUi& ui) {
    constexpr auto context = "Editor.EditorDocumentUi";
    const auto tr = [context](const char* source) {
        return QCoreApplication::translate(context, source);
    };

    ui.save_button->setText(tr("Save"));
    ui.save_button->setToolTip(tr("Save to the last path chosen with Save As; asks for a path the first time."));
    ui.save_as_button->setText(tr("Save As"));
    ui.save_as_button->setToolTip(tr("Build or copy this independent editor session to a chosen path."));
    ui.build_button->setText(tr("Build"));
    ui.build_button->setToolTip(tr("Validate and rebuild the in-memory editor object."));
    ui.extract_button->setText(tr("Extract"));
    ui.extract_button->setToolTip(tr("Write this independent editor session's current bytes to a chosen folder."));

    ui.table_label->setText(tr("Table"));
    ui.table_name_edit->setToolTip(tr("Rename the UTF table."));
    ui.apply_table_name_button->setText(tr("Rename Table"));
    ui.add_row_button->setText(tr("Add Row"));
    ui.remove_row_button->setText(tr("Remove Row"));
    ui.add_column_button->setText(tr("Add Column"));
    ui.remove_column_button->setText(tr("Remove Column"));

    ui.archive_kind_label->setText(tr("Archive"));
    ui.add_archive_file_button->setText(tr("Add File"));
    ui.replace_archive_file_button->setText(tr("Replace File"));
    ui.remove_archive_file_button->setText(tr("Remove Entry"));
    ui.move_archive_entry_up_button->setText(tr("Move Up"));
    ui.move_archive_entry_down_button->setText(tr("Move Down"));
    ui.rename_archive_entry_button->setText(tr("Rename Entry"));
    ui.reserve_afs_id_button->setText(tr("Reserve ID"));
    ui.set_afs_timestamp_button->setText(tr("Set Timestamp"));
    ui.set_archive_wave_id_button->setText(tr("Set Wave ID"));
    ui.batch_awb_wave_ids_button->setText(tr("Batch Wave IDs"));
    ui.archive_entry_options_button->setText(tr("Entry Props"));
    ui.archive_options_button->setText(tr("Options"));
    ui.archive_compression_button->setText(tr("Compression"));
    ui.archive_compress_all_action->setText(tr("Compress all on save"));
    ui.archive_store_all_action->setText(tr("Store all uncompressed"));
    ui.archive_compression_button->setToolTip(tr("Set the save-time compression policy for every CPK entry."));
    ui.import_afs_als_button->setText(tr("Import ALS"));
    ui.export_afs_header_button->setText(tr("Export Header"));
    ui.import_cvm_script_button->setText(tr("Import CVS"));
    ui.export_cvm_script_button->setText(tr("Export CVS"));
    ui.extract_archive_entry_button->setText(tr("Extract Entry"));
    ui.extract_raw_archive_entry_button->setText(tr("Extract Raw"));

    ui.transform_kind_label->setText(tr("Transform"));
    ui.encode_transform_button->setText(tr("Encode from WAV"));
    ui.decode_transform_button->setText(tr("Decode WAV"));
    ui.decrypt_transform_button->setText(tr("Decrypt"));
    ui.encrypt_transform_button->setText(tr("Encrypt"));
    ui.rebuild_transform_button->setText(tr("Rebuild"));
    ui.transform_options_button->setText(tr("Options"));
    ui.extract_transform_button->setText(tr("Extract"));
    ui.adx_container_build_button->setText(tr("Build from ADX"));
    ui.csb_directory_build_button->setText(tr("Build from Folder"));
    ui.media_build_wizard_button->setText(tr("Build Wizard"));
    ui.editor_mux_preview_button->setText(tr("Preview"));
    ui.open_acb_awb_button->setText(tr("Open AWB"));
    ui.export_acb_awb_button->setText(tr("Export AWB"));
    ui.add_transform_entry_button->setText(tr("Add"));
    ui.replace_transform_entry_button->setText(tr("Replace"));
    ui.remove_transform_entry_button->setText(tr("Remove"));
    ui.move_transform_entry_up_button->setText(tr("Up"));
    ui.move_transform_entry_down_button->setText(tr("Down"));
    ui.rename_transform_entry_button->setText(tr("Rename"));
    ui.toggle_transform_entry_flag_button->setText(tr("Toggle"));
    ui.transform_filter_edit->setPlaceholderText(tr("Filter rows"));
    ui.transform_filter_edit->setToolTip(tr("Filter transform rows by field or value."));

    ui.local_key_label->setText(tr("Local CRI key"));
    ui.local_key_type->setItemText(0, tr("No key"));
    ui.local_key_type->setItemText(1, tr("Type 8 string"));
    ui.local_key_type->setItemText(2, tr("Type 9 number"));
    ui.local_key_type->setItemText(3, tr("Key triplet"));
    ui.local_key_type->setToolTip(tr("Choose the ADX/AHX key representation explicitly."));
    ui.cri_key_edit->setPlaceholderText(tr("No key"));
    ui.cri_key_edit->setToolTip(tr("This key belongs only to this editor tab and does not change the global CRI key."));
    ui.cri_key_base->setItemText(0, tr("hex"));
    ui.cri_key_base->setItemText(1, tr("dec"));
    ui.adx_subkey_label->setText(tr("Subkey"));
    ui.adx_triplet_start_label->setText(tr("Start"));
    ui.adx_triplet_mult_label->setText(tr("Mult"));
    ui.adx_triplet_add_label->setText(tr("Add"));
    ui.cvm_scramble_check->setAccessibleName(tr("Scramble CVM metadata on save"));
    ui.cvm_scramble_check->setToolTip(tr("Write a scrambled CVM TOC using this tab's key string. Reading remains automatic."));
    ui.cvm_scramble_label->setText(tr("Scramble on save"));
    ui.apply_cri_key_button->setText(tr("Apply locally"));

    ui.log_toggle_button->setText(tr("Log"));
    ui.log_toggle_button->setToolTip(tr("Show or hide editor session log."));
    ui.media.audio_label->setText(tr("Audio channel"));
    ui.media.audio_combo->setToolTip(tr("Choose which audio stream to preview with the video."));
    ui.media.audio_popup->setToolTip(tr("Show mux audio choices"));
    ui.media.audio_popup->setAccessibleName(tr("Show mux audio choices"));
    ui.media.subtitle_label->setText(tr("Subtitles"));
    ui.media.subtitle_combo->setToolTip(tr("Choose which subtitle track to display."));
    ui.media.subtitle_popup->setToolTip(tr("Show mux subtitle choices"));
    ui.media.subtitle_popup->setAccessibleName(tr("Show mux subtitle choices"));
    ui.media.play_button->setText(tr("Play"));
    if (!ui.media.play_button->isEnabled()) {
        ui.media.status_label->setText(tr("No playable media selected"));
    }
    ui.media.volume_label->setToolTip(tr("Volume"));
    ui.media.volume_label->setAccessibleName(tr("Volume"));
    ui.media.volume_slider->setToolTip(tr("Playback volume"));
    ui.media.volume_slider->setAccessibleName(tr("Playback volume"));
    ui.media.loop_toggle->setText(tr("Loop selected range"));

    ui.schema_table->setHorizontalHeaderLabels({
        tr("Column"),
        tr("Type"),
        tr("Flags"),
        tr("Default"),
        tr("Default Offset"),
        tr("Row Offset"),
        tr("Index")
    });
    ui.value_label->setText(tr("Value"));
    ui.value_edit->setToolTip(tr("Edit the selected UTF cell. Binary and GUID values use hex byte text."));
    ui.apply_value_button->setText(tr("Apply Value"));
    ui.rename_column_button->setText(tr("Rename Column"));
    ui.replace_binary_button->setText(tr("Replace Binary From File"));
    ui.replace_binary_button->setToolTip(tr("For UTF VLData cells, replace this editor session's binary value without mutating the browser-loaded object."));
    ui.preview_tabs->setTabText(0, tr("Preview"));
    ui.preview_tabs->setTabText(1, tr("Raw"));
}

void refresh_archive_document_ui(
    EditorDocumentUi& ui,
    const ArchiveSessionView& view,
    const DecryptionKeys& keys
) {
    ui.table->hide();
    ui.field_table->hide();
    ui.utf_toolbar->hide();
    ui.utf_grid->hide();
    ui.transform_toolbar->hide();
    ui.transform_table->hide();
    ui.schema_table->hide();
    ui.payload_table->hide();
    ui.hex_preview->hide();
    ui.apply_value_button->setEnabled(false);
    ui.rename_column_button->setEnabled(false);
    ui.replace_binary_button->setEnabled(false);
    ui.value_edit->clear();
    ui.utf_edit_panel->hide();
    ui.binary_actions_panel->hide();

    ui.archive_toolbar->show();
    ui.archive_table->show();
    ui.archive_kind_label->setText(archive_kind_name(view.kind));
    const bool is_afs_archive = view.kind == ArchiveKind::Afs;
    ui.rename_archive_entry_button->setVisible(is_afs_archive);
    ui.reserve_afs_id_button->setVisible(is_afs_archive);
    ui.set_afs_timestamp_button->setVisible(is_afs_archive);
    ui.set_archive_wave_id_button->setVisible(view.kind == ArchiveKind::Awb);
    ui.batch_awb_wave_ids_button->setVisible(view.kind == ArchiveKind::Awb);
    ui.archive_entry_options_button->setVisible(view.kind == ArchiveKind::Cpk || view.kind == ArchiveKind::Cvm);
    ui.add_archive_file_button->setText(view.kind == ArchiveKind::Cpk ? QCoreApplication::translate("Editor.EditorDocumentUi", "Add...") : QCoreApplication::translate("Editor.EditorDocumentUi", "Add File"));
    ui.add_archive_file_button->setToolTip(view.kind == ArchiveKind::Cpk
        ? QCoreApplication::translate("Editor.EditorDocumentUi", "Add multiple files or a folder tree while preserving paths relative to the selected folder.")
        : QString{});
    const bool can_reorder_archive = view.kind == ArchiveKind::Afs ||
                                     view.kind == ArchiveKind::Awb ||
                                     view.kind == ArchiveKind::Acx ||
                                     view.kind == ArchiveKind::Cpk ||
                                     view.kind == ArchiveKind::Cvm;
    ui.move_archive_entry_up_button->setVisible(can_reorder_archive);
    ui.move_archive_entry_down_button->setVisible(can_reorder_archive);
    ui.archive_options_button->setVisible(view.kind == ArchiveKind::Afs || view.kind == ArchiveKind::Awb ||
                                          view.kind == ArchiveKind::Cpk || view.kind == ArchiveKind::Cvm);
    ui.archive_compression_button->setVisible(view.kind == ArchiveKind::Cpk);
    ui.import_afs_als_button->setVisible(is_afs_archive);
    ui.export_afs_header_button->setVisible(is_afs_archive);
    ui.import_cvm_script_button->setVisible(view.kind == ArchiveKind::Cvm);
    ui.export_cvm_script_button->setVisible(view.kind == ArchiveKind::Cvm);

    if (view.kind == ArchiveKind::Afs && view.afs != nullptr) {
        modules::afs::populate_editor_archive_table(ui.archive_table, *view.afs);
    } else if (view.kind == ArchiveKind::Awb && view.awb != nullptr) {
        modules::awb::populate_editor_archive_table(ui.archive_table, *view.awb, keys);
    } else if (view.kind == ArchiveKind::Acx && view.acx != nullptr) {
        modules::acx::populate_editor_archive_table(ui.archive_table, *view.acx);
    } else if (view.kind == ArchiveKind::Cpk && view.cpk != nullptr) {
        modules::cpk::populate_editor_archive_table(ui.archive_table, *view.cpk);
    } else if (view.kind == ArchiveKind::Cvm && view.cvm != nullptr) {
        modules::cvm::populate_editor_archive_table(ui.archive_table, *view.cvm);
    }

    for (int col = 0; col < ui.archive_table->columnCount(); ++col) {
        ui.archive_table->resizeColumnToContents(col);
    }
    if (ui.archive_table->currentRow() < 0 && ui.archive_table->rowCount() > 0) {
        ui.archive_table->setCurrentCell(0, 0);
    }
}

void refresh_transform_document_ui(
    EditorDocumentUi& ui,
    TransformKind kind,
    const TransformSessionView& view,
    const std::vector<modules::TransformDetailRow>& rows,
    QString filter_text
) {
    ui.table->hide();
    ui.field_table->hide();
    ui.utf_toolbar->hide();
    ui.utf_grid->hide();
    ui.schema_table->hide();
    ui.apply_value_button->setEnabled(false);
    ui.rename_column_button->setEnabled(false);
    ui.replace_binary_button->setEnabled(false);
    ui.value_edit->clear();
    ui.utf_edit_panel->hide();
    ui.binary_actions_panel->hide();
    ui.archive_toolbar->hide();
    ui.archive_table->hide();

    ui.transform_toolbar->show();
    ui.transform_table->show();
    ui.transform_kind_label->setText(transform_kind_name(kind));
    ui.encode_transform_button->setVisible(kind == TransformKind::AudioEncode ||
                                           kind == TransformKind::Adx ||
                                           kind == TransformKind::Hca);
    ui.decode_transform_button->setVisible(kind == TransformKind::Adx || kind == TransformKind::Hca);
    ui.decrypt_transform_button->setVisible(kind == TransformKind::Adx || kind == TransformKind::Hca);
    ui.encrypt_transform_button->setVisible(kind == TransformKind::Hca);
    ui.rebuild_transform_button->setVisible(kind == TransformKind::Adx || kind == TransformKind::Hca ||
                                            (kind == TransformKind::Aax && view.aax != nullptr) ||
                                            (kind == TransformKind::Aix && view.aix != nullptr) ||
                                            (kind == TransformKind::Sfd && view.sfd != nullptr) ||
                                            (kind == TransformKind::Csb && view.csb != nullptr));
    ui.transform_options_button->setVisible((kind == TransformKind::Adx && view.adx != nullptr) ||
                                            (kind == TransformKind::Hca && view.hca != nullptr));
    ui.extract_transform_button->setVisible((kind == TransformKind::Aax && view.aax != nullptr) ||
                                            (kind == TransformKind::Aix && view.aix != nullptr) ||
                                            (kind == TransformKind::Usm && view.usm != nullptr) ||
                                            (kind == TransformKind::Sfd && view.sfd != nullptr) ||
                                            (kind == TransformKind::Csb && view.csb != nullptr) ||
                                            (kind == TransformKind::Acb && view.acb != nullptr));
    ui.adx_container_build_button->setVisible(kind == TransformKind::Aax || kind == TransformKind::Aix);
    ui.csb_directory_build_button->setVisible(kind == TransformKind::Csb);
    ui.media_build_wizard_button->setVisible(kind == TransformKind::MediaBuild ||
                                             kind == TransformKind::Usm ||
                                             kind == TransformKind::Sfd);
    ui.editor_mux_preview_button->setVisible(
        (kind == TransformKind::Usm && view.usm != nullptr) ||
        (kind == TransformKind::Sfd && view.sfd != nullptr) ||
        (kind == TransformKind::Adx && view.adx != nullptr) ||
        (kind == TransformKind::Hca && view.hca != nullptr) ||
        (kind == TransformKind::Aax && view.aax != nullptr)
    );
    const bool has_acb_awb = kind == TransformKind::Acb && view.acb != nullptr && view.acb->load_awb().has_value();
    ui.open_acb_awb_button->setVisible(kind == TransformKind::Acb);
    ui.export_acb_awb_button->setVisible(kind == TransformKind::Acb);
    ui.open_acb_awb_button->setEnabled(has_acb_awb);
    ui.export_acb_awb_button->setEnabled(has_acb_awb);
    const bool edits_entries = (kind == TransformKind::Aax && view.aax != nullptr) ||
        (kind == TransformKind::Aix && view.aix != nullptr) ||
        (kind == TransformKind::Csb && view.csb != nullptr);
    ui.add_transform_entry_button->setVisible(edits_entries);
    ui.replace_transform_entry_button->setVisible(edits_entries);
    ui.remove_transform_entry_button->setVisible(edits_entries);
    ui.move_transform_entry_up_button->setVisible(edits_entries);
    ui.move_transform_entry_down_button->setVisible(edits_entries);
    ui.rename_transform_entry_button->setVisible(kind == TransformKind::Csb && view.csb != nullptr);
    ui.toggle_transform_entry_flag_button->setVisible(
        (kind == TransformKind::Aax && view.aax != nullptr) ||
        (kind == TransformKind::Csb && view.csb != nullptr));
    ui.add_transform_entry_button->setText(
        kind == TransformKind::Aix ? QCoreApplication::translate("Editor.EditorDocumentUi", "Add...") : QCoreApplication::translate("Editor.EditorDocumentUi", "Add"));
    ui.add_transform_entry_button->setToolTip(kind == TransformKind::Aix
        ? QCoreApplication::translate("Editor.EditorDocumentUi", "Add a complete segment or a complete layer of ADX streams.")
        : QString{});
    ui.toggle_transform_entry_flag_button->setText(
        kind == TransformKind::Aax ? QCoreApplication::translate("Editor.EditorDocumentUi", "Toggle Loop") : QCoreApplication::translate("Editor.EditorDocumentUi", "Toggle Streamed"));
    ui.transform_filter_edit->setVisible(kind == TransformKind::Usm);
    ui.transform_filter_edit->setPlaceholderText(
        kind == TransformKind::Usm
            ? QCoreApplication::translate("Editor.EditorDocumentUi", "Filter rows: sfv, sfa, sbt, ch 0, utf...")
            : QCoreApplication::translate("Editor.EditorDocumentUi", "Filter rows")
    );

    std::vector<modules::TransformDetailRow> filtered_rows;
    filtered_rows.reserve(rows.size());
    for (const auto& detail : rows) {
        if (matches_transform_filter(detail, filter_text)) {
            filtered_rows.push_back(detail);
        }
    }
    ui.transform_model->set_rows(std::move(filtered_rows));
    if (ui.transform_table->horizontalScrollBar() != nullptr) {
        ui.transform_table->horizontalScrollBar()->setValue(0);
    }
    if (!ui.transform_table->currentIndex().isValid() && ui.transform_model->rowCount() > 0) {
        ui.transform_table->setCurrentIndex(ui.transform_model->index(0, 0));
        ui.transform_table->selectRow(0);
    }
}

void refresh_document_info_ui(EditorDocumentUi& ui, QWidget* parent, const EditorDocumentInfoView& view) {
    while (auto* item = ui.info_grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const auto all_rows = document_info_rows(view);
    std::vector<InfoRow> rows;
    rows.reserve(6);
    static const std::array<std::string, 6> header_fields{
        cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Session"), cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Format"), cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Bytes"), cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Source path"), cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Validation"), cristudio::i18n::translate_utf8("Editor.EditorDocumentUi", "Inspector kind")
    };
    for (const auto& field : header_fields) {
        const auto found = std::ranges::find(all_rows, field, &InfoRow::name);
        if (found != all_rows.end()) {
            rows.push_back(*found);
        }
    }
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto column = (i % 2) * 2;
        const auto row = i / 2;
        ui.info_grid->addWidget(dim_label(utf8_to_qstring(rows[static_cast<size_t>(i)].name), parent), row, column, Qt::AlignVCenter);
        ui.info_grid->addWidget(value_label(utf8_to_qstring(rows[static_cast<size_t>(i)].value), parent), row, column + 1, Qt::AlignVCenter);
    }
}

} // namespace cristudio
