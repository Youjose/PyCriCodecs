#include "../main_window.hpp"

#include "../editor_workspace.hpp"
#include "../modules/acb/acb_cue_view.hpp"
#include "../path_text.hpp"
#include "../shared/document_preview_router.hpp"
#include "ui_helpers.hpp"

#include <QCoreApplication>
#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QApplication>
#include <QComboBox>
#include <QClipboard>
#include <QCheckBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPalette>
#include <QScrollArea>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeView>
#include <QWidget>

#include <algorithm>
#include <numeric>
#include <utility>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace cristudio {

namespace {

QString copied_row_text(const QAbstractItemModel* model, const QModelIndex& index) {
    if (model == nullptr || !index.isValid()) {
        return {};
    }
    QStringList cells;
    cells.reserve(model->columnCount(index.parent()));
    for (int column = 0; column < model->columnCount(index.parent()); ++column) {
        cells.push_back(model->index(index.row(), column, index.parent()).data(Qt::DisplayRole).toString());
    }
    return cells.join(QLatin1Char('\t'));
}

bool is_usm_recovery_entry(const EntrySummary& entry) {
    const auto contains_usm = [](const std::string& text) {
        auto lower = QString::fromStdString(text).toLower();
        return lower == QStringLiteral("usm") || lower.contains(QStringLiteral("usm/")) ||
            lower.endsWith(QStringLiteral(".usm"));
    };
    return contains_usm(entry.source_format) || contains_usm(entry.nested_source_format) ||
        contains_usm(entry.type) || contains_usm(entry.name);
}

std::optional<AdxRecoveryKind> adx_recovery_kind(const EntrySummary& entry) {
    const auto detected = QString::fromStdString(
        entry.type + " " + entry.detail + " " +
        entry.source_format + " " + entry.nested_source_format).toLower();
    if (detected.contains(QStringLiteral("ahx"))) {
        return AdxRecoveryKind::Ahx;
    }
    if (detected.contains(QStringLiteral("adx"))) {
        return AdxRecoveryKind::Adx;
    }
    return std::nullopt;
}

std::optional<AdxRecoveryKind> adx_recovery_kind(const LoadedDocument& document) {
    for (const auto& row : document.info) {
        if (info_row_id(row) == "AHX routed") {
            const auto value = info_row_value_id(row);
            return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()))
                           .compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0
                ? std::optional(AdxRecoveryKind::Ahx)
                : std::optional(AdxRecoveryKind::Adx);
        }
    }
    const auto text = QString::fromUtf8(document_format_id(document)).toLower();
    if (text.contains(QStringLiteral("ahx"))) {
        return AdxRecoveryKind::Ahx;
    }
    if (text.contains(QStringLiteral("adx"))) {
        return AdxRecoveryKind::Adx;
    }
    return std::nullopt;
}

void append_document_hca_recovery_sources(
    const LoadedDocument& document,
    std::vector<HcaRecoverySource>& sources
) {
    const auto tag = QString::fromUtf8(
        document_format_id(document).data(),
        static_cast<qsizetype>(document_format_id(document).size())
    ).toLower();
    if (tag.contains(QStringLiteral("cpk"))) {
        const auto initial_size = sources.size();
        for (const auto& entry : document.entries) {
            if (entry.has_source && supports_hca_key_recovery(entry)) {
                sources.push_back(make_hca_recovery_source(entry));
            }
        }
        if (sources.size() != initial_size) {
            return;
        }
    }
    if (supports_hca_key_recovery(document)) {
        sources.push_back(make_hca_recovery_source(document));
    }
}

void append_document_adx_recovery_sources(
    const LoadedDocument& document,
    std::vector<AdxRecoverySource>& sources
) {
    const auto text = QString::fromUtf8(
        document_format_id(document).data(),
        static_cast<qsizetype>(document_format_id(document).size())
    ).toLower();
    if (text.contains(QStringLiteral("aax")) ||
        text.contains(QStringLiteral("awb")) ||
        text.contains(QStringLiteral("acb")) ||
        text.contains(QStringLiteral("csb")) ||
        text.contains(QStringLiteral("cpk"))) {
        sources.push_back(make_adx_recovery_source(document));
        return;
    }
    const auto initial_size = sources.size();
    for (const auto& entry : document.entries) {
        if (entry.has_source && adx_recovery_kind(entry).has_value()) {
            sources.push_back(make_adx_recovery_source(entry));
        }
    }
    if (sources.size() != initial_size) {
        return;
    }
    if (adx_recovery_kind(document).has_value()) {
        sources.push_back(make_adx_recovery_source(document));
    }
}

size_t open_entries_in_editor(
    EditorWorkspace& workspace,
    const EntryTableModel& model,
    const QModelIndexList& rows,
    const DecryptionKeys& keys
) {
    size_t opened = 0;
    for (const auto& row : rows) {
        const auto* summary = model.summary_at(row);
        if (summary == nullptr || !summary->has_source || !supports_editor(*summary)) {
            continue;
        }
        EditorOpenRequest request{
            .source_kind = EditorOpenRequest::SourceKind::ArchiveEntry,
            .display_name = summary->name,
            .detected_format = summary->type.empty() ? summary->source_format : summary->type,
            .source_archive_path = summary->source_path,
            .source_archive_format = summary->source_format,
            .source_index = summary->source_index,
            .keys = keys,
            .entry = *summary,
        };
        if (auto bytes = load_embedded_entry_bytes(*summary, keys)) {
            request.source_bytes = std::move(*bytes);
        }
        workspace.open_request(std::move(request));
        ++opened;
    }
    return opened;
}

} // namespace

void MainWindow::select_first_loaded_file() {
    if (m_file_proxy == nullptr || m_file_view == nullptr || m_file_proxy->rowCount() <= 0) {
        return;
    }

    const auto first = m_file_proxy->index(0, 0);
    m_file_view->setCurrentIndex(first);
    m_file_view->scrollTo(first, QAbstractItemView::PositionAtTop);
}

bool MainWindow::select_first_entry() {
    if (m_entry_proxy == nullptr || m_entry_view == nullptr || m_entry_proxy->rowCount() <= 0) {
        return false;
    }

    const auto find_leaf = [this](const auto& self, const QModelIndex& parent) -> QModelIndex {
        const auto rows = m_entry_proxy->rowCount(parent);
        for (int row = 0; row < rows; ++row) {
            const auto index = m_entry_proxy->index(row, 0, parent);
            if (m_entry_proxy->rowCount(index) == 0) {
                return index;
            }
            if (auto child = self(self, index); child.isValid()) {
                return child;
            }
        }
        return {};
    };

    const auto entry = find_leaf(find_leaf, {});
    if (!entry.isValid()) {
        return false;
    }

    m_entry_view->setCurrentIndex(entry);
    m_entry_view->scrollTo(entry, QAbstractItemView::PositionAtCenter);
    return true;
}


bool MainWindow::open_first_loaded_file_in_editor() {
    select_first_loaded_file();
    if (m_file_view == nullptr || !m_file_view->currentIndex().isValid()) {
        return false;
    }
    open_selected_files_in_editor();
    return true;
}

bool MainWindow::open_first_entry_in_editor() {
    if (!select_first_entry()) {
        return false;
    }
    open_selected_entries_in_editor();
    return true;
}


void MainWindow::clear_loaded_files() {
    ++m_preview_request_id;
    m_hca_key_recovery_stop_source.request_stop();
    ++m_hca_key_recovery_request_id;
    ++m_usm_key_recovery_request_id;
    ++m_adx_key_recovery_request_id;
    ++m_aac_key_recovery_request_id;
    m_pending_preview_entry = std::nullopt;
    m_pending_mux_preview = std::nullopt;
    reset_audio_preview();
    m_queued_load_paths.clear();
    if (load_running()) {
        m_drop_active_load_result = true;
    }
    m_file_filter->clear();
    m_entry_filter->clear();
    m_file_model->clear();
    m_entry_model->clear();
    show_document(nullptr);

    m_nested_title->setText(QCoreApplication::translate("MainWindow.BrowserView", "Entry preview"));
    m_nested_subtitle->setText(QCoreApplication::translate("MainWindow.BrowserView", "Select a supported embedded file."));
    populate_info_grid(m_nested_info_grid, {});
    m_nested_image->clear();
    m_nested_image_scroll->hide();
    m_nested_entry_model->clear();
    m_nested_entry_view->hide();
    m_nested_body->setMaximumHeight(QWIDGETSIZE_MAX);
    m_nested_body->clear();
    if (m_toggle_preview_action != nullptr) {
        m_toggle_preview_action->setChecked(false);
    }
    if (m_preview_panel_button != nullptr) {
        m_preview_panel_button->setChecked(false);
    }
    toggle_preview_panel();

    append_log(QCoreApplication::translate("MainWindow.BrowserView", "Cleared loaded files"));
    statusBar()->showMessage(QCoreApplication::translate("MainWindow.BrowserView", "Cleared loaded files"), 3000);
    update_file_list_status();
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
    update_memory_usage_label();
}

void MainWindow::unload_selected_files() {
    if (m_file_view == nullptr || m_file_proxy == nullptr || m_file_model == nullptr || m_file_view->selectionModel() == nullptr) {
        return;
    }

    std::vector<int> source_rows;
    const auto selected = m_file_view->selectionModel()->selectedRows();
    source_rows.reserve(static_cast<size_t>(selected.size()));
    for (const auto& index : selected) {
        const auto source = m_file_proxy->mapToSource(index);
        if (source.isValid()) {
            source_rows.push_back(source.row());
        }
    }
    if (source_rows.empty()) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.BrowserView", "No loaded files selected"), 3000);
        return;
    }

    ++m_preview_request_id;
    reset_audio_preview();
    m_file_model->remove_rows(std::move(source_rows));
    show_document(nullptr);
    update_file_list_status();
    append_log(QCoreApplication::translate("MainWindow.BrowserView", "Unloaded selected files"));
    statusBar()->showMessage(QCoreApplication::translate("MainWindow.BrowserView", "Unloaded selected files"), 3000);
}

void MainWindow::open_selected_files_in_editor() {
    if (m_editor_workspace == nullptr || m_file_view == nullptr || m_file_proxy == nullptr || m_file_model == nullptr || m_file_view->selectionModel() == nullptr) {
        return;
    }

    size_t opened = 0;
    const auto selected = m_file_view->selectionModel()->selectedRows();
    for (const auto& index : selected) {
        const auto source = m_file_proxy->mapToSource(index);
        if (!source.isValid()) {
            continue;
        }
        const auto* document = ensure_loaded_document(source.row());
        if (document == nullptr || !supports_editor(*document)) {
            continue;
        }

        EditorOpenRequest request;
        request.source_kind = EditorOpenRequest::SourceKind::Path;
        request.display_name = document->display_name;
        request.detected_format = document->loader_tag.empty() ? document->format : document->loader_tag;
        request.source_path = document->path;
        request.keys = m_decryption_keys;
        request.document = *document;
        m_editor_workspace->open_request(std::move(request));
        ++opened;
    }

    if (opened == 0) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.BrowserView", "No loaded files selected for Editor"), 3000);
        return;
    }
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QCoreApplication::translate(
        "MainWindow.BrowserView",
        "Opened %n loaded file(s) in Editor",
        nullptr,
        static_cast<int>(opened)));
}

void MainWindow::open_selected_entries_in_editor() {
    if (m_editor_workspace == nullptr || m_entry_view == nullptr || m_entry_proxy == nullptr || m_entry_model == nullptr || m_entry_view->selectionModel() == nullptr) {
        return;
    }

    QModelIndexList source_rows;
    const auto selected = m_entry_view->selectionModel()->selectedRows();
    source_rows.reserve(selected.size());
    for (const auto& index : selected) {
        const auto source = m_entry_proxy->mapToSource(index);
        if (source.isValid()) {
            source_rows.push_back(source);
        }
    }
    const auto opened = open_entries_in_editor(
        *m_editor_workspace, *m_entry_model, source_rows, m_decryption_keys);

    if (opened == 0) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.BrowserView", "No extractable archive entries selected for Editor"), 3000);
        return;
    }
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QCoreApplication::translate(
        "MainWindow.BrowserView",
        "Opened %n archive entry(s) in Editor",
        nullptr,
        static_cast<int>(opened)));
}

void MainWindow::open_selected_nested_entries_in_editor() {
    if (m_editor_workspace == nullptr || m_nested_entry_view == nullptr || m_nested_entry_model == nullptr || m_nested_entry_view->selectionModel() == nullptr) {
        return;
    }

    const auto selected = m_nested_entry_view->selectionModel()->selectedRows();
    const auto opened = open_entries_in_editor(
        *m_editor_workspace, *m_nested_entry_model, selected, m_decryption_keys);

    if (opened == 0) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.BrowserView", "No extractable preview entries selected for Editor"), 3000);
        return;
    }
    if (m_workspace_tabs != nullptr) {
        m_workspace_tabs->setCurrentWidget(m_editor_workspace);
    }
    append_log(QCoreApplication::translate(
        "MainWindow.BrowserView",
        "Opened %n preview entry(s) in Editor",
        nullptr,
        static_cast<int>(opened)));
}

std::vector<ExtractionTarget> MainWindow::selected_file_targets() const {
    std::vector<ExtractionTarget> targets;
    if (m_file_view == nullptr || m_file_proxy == nullptr || m_file_model == nullptr || m_file_view->selectionModel() == nullptr) {
        return targets;
    }

    const auto selected = m_file_view->selectionModel()->selectedRows();
    targets.reserve(static_cast<size_t>(selected.size()));
    for (const auto& index : selected) {
        const auto source = m_file_proxy->mapToSource(index);
        if (!source.isValid()) {
            continue;
        }
        if (const auto* document = m_file_model->document_at(source.row()); document != nullptr) {
            targets.push_back(ExtractionTarget{
                .kind = ExtractionTarget::Kind::Document,
                .document = *document,
            });
        }
    }
    return targets;
}

std::vector<ExtractionTarget> MainWindow::all_file_targets() const {
    std::vector<ExtractionTarget> targets;
    if (m_file_model == nullptr) {
        return targets;
    }

    const auto rows = m_file_model->rowCount();
    targets.reserve(static_cast<size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        if (const auto* document = m_file_model->document_at(row); document != nullptr) {
            targets.push_back(ExtractionTarget{
                .kind = ExtractionTarget::Kind::Document,
                .document = *document,
            });
        }
    }
    return targets;
}

std::vector<ExtractionTarget> MainWindow::selected_entry_targets() const {
    std::vector<ExtractionTarget> targets;
    if (m_entry_view == nullptr || m_entry_proxy == nullptr || m_entry_model == nullptr || m_entry_view->selectionModel() == nullptr) {
        return targets;
    }

    const auto selected = m_entry_view->selectionModel()->selectedRows();
    targets.reserve(static_cast<size_t>(selected.size()));
    for (const auto& index : selected) {
        const auto source = m_entry_proxy->mapToSource(index);
        if (!source.isValid()) {
            continue;
        }
        if (const auto* summary = m_entry_model->summary_at(source);
            summary != nullptr) {
            if (summary->source_format == "ACB Cue") {
                const auto route_index =
                    m_current_acb_cue_index == summary->source_index &&
                        m_acb_cue_route_combo != nullptr &&
                        m_acb_cue_route_combo->currentIndex() >= 0
                    ? std::optional<uint32_t>{
                          m_acb_cue_route_combo->currentData().toUInt()}
                    : std::nullopt;
                if (auto target = acb_cue_extraction_target(
                        summary->source_index,
                        route_index)) {
                    targets.push_back(std::move(*target));
                }
            } else if (summary->has_source) {
                targets.push_back(ExtractionTarget{
                    .kind = ExtractionTarget::Kind::Entry,
                    .entry = *summary,
                });
            }
        }
    }
    return targets;
}

std::vector<ExtractionTarget> MainWindow::selected_nested_entry_targets() const {
    std::vector<ExtractionTarget> targets;
    if (m_nested_entry_view == nullptr || m_nested_entry_model == nullptr || m_nested_entry_view->selectionModel() == nullptr) {
        return targets;
    }

    const auto selected = m_nested_entry_view->selectionModel()->selectedRows();
    targets.reserve(static_cast<size_t>(selected.size()));
    for (const auto& index : selected) {
        if (!index.isValid()) {
            continue;
        }
        if (const auto* summary = m_nested_entry_model->summary_at(index); summary != nullptr && summary->has_source) {
            targets.push_back(ExtractionTarget{
                .kind = ExtractionTarget::Kind::Entry,
                .entry = *summary,
            });
        }
    }
    return targets;
}

std::vector<ExtractionTarget> MainWindow::current_preview_entry_targets() const {
    if (m_showing_acb_cues && m_current_acb_cue_index.has_value() &&
        m_acb_cue_route_combo != nullptr &&
        m_acb_cue_route_combo->currentIndex() >= 0) {
        if (auto target = acb_cue_extraction_target(
                *m_current_acb_cue_index,
                m_acb_cue_route_combo->currentData().toUInt())) {
            return {std::move(*target)};
        }
    }
    if (!m_current_preview_entry || !m_current_preview_entry->has_source) {
        return {};
    }
    return {ExtractionTarget{
        .kind = ExtractionTarget::Kind::Entry,
        .entry = *m_current_preview_entry,
    }};
}

std::optional<ExtractionTarget> MainWindow::acb_cue_extraction_target(
    uint32_t cue_index,
    std::optional<uint32_t> route_index) const {
    if (m_acb_cue_sheet == nullptr ||
        cue_index >= m_acb_cue_sheet->cues.size()) {
        return std::nullopt;
    }
    const auto& cue = m_acb_cue_sheet->cues[cue_index];
    if (cue.routes.empty()) {
        return std::nullopt;
    }
    const auto selected_route = route_index.value_or(0);
    if (selected_route >= cue.routes.size()) {
        return std::nullopt;
    }
    const auto plan_index = cue.routes[selected_route].plan_index;
    if (plan_index >= m_acb_cue_sheet->plans.size()) {
        return std::nullopt;
    }
    const auto& plan = m_acb_cue_sheet->plans[plan_index];
    return ExtractionTarget{
        .kind = ExtractionTarget::Kind::AcbCue,
        .acb_path = m_acb_cue_source_path,
        .acb_cue_index = cue.cue_index,
        .acb_plan_signature = plan.semantic_signature,
        .acb_output_name = plan.output_name,
        .acb_include_empty_holds =
            m_current_acb_cue_index == cue_index &&
            m_acb_include_empty_holds != nullptr &&
            m_acb_include_empty_holds->isChecked(),
    };
}

std::vector<HcaRecoverySource> MainWindow::selected_file_recovery_sources() const {
    std::vector<HcaRecoverySource> sources;
    if (m_file_view == nullptr || m_file_proxy == nullptr || m_file_model == nullptr || m_file_view->selectionModel() == nullptr) {
        return sources;
    }

    const auto selected = m_file_view->selectionModel()->selectedRows();
    sources.reserve(static_cast<size_t>(selected.size()));
    for (const auto& index : selected) {
        const auto source = m_file_proxy->mapToSource(index);
        if (source.isValid()) {
            if (const auto* document = m_file_model->document_at(source.row()); document != nullptr) {
                append_document_hca_recovery_sources(*document, sources);
            }
        }
    }
    return sources;
}

std::vector<HcaRecoverySource> MainWindow::all_file_recovery_sources() const {
    std::vector<HcaRecoverySource> sources;
    if (m_file_model == nullptr) {
        return sources;
    }

    const auto rows = m_file_model->rowCount();
    sources.reserve(static_cast<size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        if (const auto* document = m_file_model->document_at(row); document != nullptr) {
            append_document_hca_recovery_sources(*document, sources);
        }
    }
    return sources;
}

std::vector<HcaRecoverySource> MainWindow::selected_entry_recovery_sources() const {
    std::vector<HcaRecoverySource> sources;
    if (m_entry_view == nullptr || m_entry_proxy == nullptr || m_entry_model == nullptr || m_entry_view->selectionModel() == nullptr) {
        return sources;
    }

    const auto selected = m_entry_view->selectionModel()->selectedRows();
    sources.reserve(static_cast<size_t>(selected.size()));
    for (const auto& index : selected) {
        const auto source = m_entry_proxy->mapToSource(index);
        if (source.isValid()) {
            if (const auto* entry = m_entry_model->summary_at(source); entry != nullptr && entry->has_source) {
                if (supports_hca_key_recovery(*entry)) {
                    sources.push_back(make_hca_recovery_source(*entry));
                }
            }
        }
    }
    return sources;
}

std::vector<HcaRecoverySource> MainWindow::selected_nested_entry_recovery_sources() const {
    std::vector<HcaRecoverySource> sources;
    if (m_nested_entry_view == nullptr || m_nested_entry_model == nullptr || m_nested_entry_view->selectionModel() == nullptr) {
        return sources;
    }

    const auto selected = m_nested_entry_view->selectionModel()->selectedRows();
    sources.reserve(static_cast<size_t>(selected.size()));
    for (const auto& index : selected) {
        if (index.isValid()) {
            if (const auto* entry = m_nested_entry_model->summary_at(index); entry != nullptr && entry->has_source) {
                if (supports_hca_key_recovery(*entry)) {
                    sources.push_back(make_hca_recovery_source(*entry));
                }
            }
        }
    }
    return sources;
}

std::vector<HcaRecoverySource> MainWindow::current_preview_recovery_sources() const {
    if (m_current_preview_entry && m_current_preview_entry->has_source &&
        supports_hca_key_recovery(*m_current_preview_entry)) {
        return {make_hca_recovery_source(*m_current_preview_entry)};
    }
    std::vector<HcaRecoverySource> sources;
    if (m_file_view != nullptr && m_file_proxy != nullptr && m_file_model != nullptr &&
        m_file_view->currentIndex().isValid()) {
        const auto source = m_file_proxy->mapToSource(m_file_view->currentIndex());
        if (const auto* document = source.isValid() ? m_file_model->document_at(source.row()) : nullptr;
            document != nullptr) {
            append_document_hca_recovery_sources(*document, sources);
        }
    }
    return sources;
}

std::vector<AacRecoverySource> MainWindow::selected_file_aac_recovery_sources() const {
    std::vector<AacRecoverySource> sources;
    if (m_file_view == nullptr || m_file_proxy == nullptr || m_file_model == nullptr ||
        m_file_view->selectionModel() == nullptr) {
        return sources;
    }
    for (const auto& index : m_file_view->selectionModel()->selectedRows()) {
        const auto source = m_file_proxy->mapToSource(index);
        const auto* document = source.isValid() ? m_file_model->document_at(source.row()) : nullptr;
        if (document != nullptr && supports_aac_key_recovery(*document)) {
            sources.push_back(make_aac_recovery_source(*document));
        }
    }
    return sources;
}

std::vector<AacRecoverySource> MainWindow::all_file_aac_recovery_sources() const {
    std::vector<AacRecoverySource> sources;
    if (m_file_model == nullptr) {
        return sources;
    }
    for (int row = 0; row < m_file_model->rowCount(); ++row) {
        const auto* document = m_file_model->document_at(row);
        if (document != nullptr && supports_aac_key_recovery(*document)) {
            sources.push_back(make_aac_recovery_source(*document));
        }
    }
    return sources;
}

std::vector<AacRecoverySource> MainWindow::selected_entry_aac_recovery_sources() const {
    std::vector<AacRecoverySource> sources;
    if (m_entry_view == nullptr || m_entry_proxy == nullptr || m_entry_model == nullptr ||
        m_entry_view->selectionModel() == nullptr) {
        return sources;
    }
    for (const auto& index : m_entry_view->selectionModel()->selectedRows()) {
        const auto source = m_entry_proxy->mapToSource(index);
        const auto* entry = source.isValid() ? m_entry_model->summary_at(source) : nullptr;
        if (entry != nullptr && entry->has_source && supports_aac_key_recovery(*entry)) {
            sources.push_back(make_aac_recovery_source(*entry));
        }
    }
    return sources;
}

std::vector<AacRecoverySource> MainWindow::selected_nested_entry_aac_recovery_sources() const {
    std::vector<AacRecoverySource> sources;
    if (m_nested_entry_view == nullptr || m_nested_entry_model == nullptr ||
        m_nested_entry_view->selectionModel() == nullptr) {
        return sources;
    }
    for (const auto& index : m_nested_entry_view->selectionModel()->selectedRows()) {
        const auto* entry = index.isValid() ? m_nested_entry_model->summary_at(index) : nullptr;
        if (entry != nullptr && entry->has_source && supports_aac_key_recovery(*entry)) {
            sources.push_back(make_aac_recovery_source(*entry));
        }
    }
    return sources;
}

std::vector<AacRecoverySource> MainWindow::current_preview_aac_recovery_sources() const {
    if (m_current_preview_entry && m_current_preview_entry->has_source &&
        supports_aac_key_recovery(*m_current_preview_entry)) {
        return {make_aac_recovery_source(*m_current_preview_entry)};
    }
    if (m_file_view != nullptr && m_file_proxy != nullptr && m_file_model != nullptr &&
        m_file_view->currentIndex().isValid()) {
        const auto source = m_file_proxy->mapToSource(m_file_view->currentIndex());
        const auto* document = source.isValid() ? m_file_model->document_at(source.row()) : nullptr;
        if (document != nullptr && supports_aac_key_recovery(*document)) {
            return {make_aac_recovery_source(*document)};
        }
    }
    return {};
}

std::vector<UsmRecoverySource> MainWindow::selected_file_usm_recovery_sources() const {
    std::vector<UsmRecoverySource> sources;
    if (m_file_view == nullptr || m_file_proxy == nullptr || m_file_model == nullptr || m_file_view->selectionModel() == nullptr) {
        return sources;
    }
    const auto selected = m_file_view->selectionModel()->selectedRows();
    sources.reserve(static_cast<size_t>(selected.size()));
    for (const auto& index : selected) {
        const auto source = m_file_proxy->mapToSource(index);
        if (source.isValid()) {
            if (const auto* document = m_file_model->document_at(source.row()); document != nullptr) {
                sources.push_back(make_usm_recovery_source(*document));
            }
        }
    }
    return sources;
}

std::vector<UsmRecoverySource> MainWindow::all_file_usm_recovery_sources() const {
    std::vector<UsmRecoverySource> sources;
    if (m_file_model == nullptr) {
        return sources;
    }
    const auto rows = m_file_model->rowCount();
    sources.reserve(static_cast<size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        if (const auto* document = m_file_model->document_at(row); document != nullptr) {
            sources.push_back(make_usm_recovery_source(*document));
        }
    }
    return sources;
}

std::vector<UsmRecoverySource> MainWindow::selected_entry_usm_recovery_sources() const {
    std::vector<UsmRecoverySource> sources;
    if (m_entry_view == nullptr || m_entry_proxy == nullptr || m_entry_model == nullptr || m_entry_view->selectionModel() == nullptr) {
        return sources;
    }
    const auto selected = m_entry_view->selectionModel()->selectedRows();
    sources.reserve(static_cast<size_t>(selected.size()));
    for (const auto& index : selected) {
        const auto source = m_entry_proxy->mapToSource(index);
        if (source.isValid()) {
            if (const auto* entry = m_entry_model->summary_at(source); entry != nullptr && entry->has_source) {
                sources.push_back(make_usm_recovery_source(*entry));
            }
        }
    }
    return sources;
}

std::vector<UsmRecoverySource> MainWindow::selected_nested_entry_usm_recovery_sources() const {
    std::vector<UsmRecoverySource> sources;
    if (m_nested_entry_view == nullptr || m_nested_entry_model == nullptr || m_nested_entry_view->selectionModel() == nullptr) {
        return sources;
    }
    const auto selected = m_nested_entry_view->selectionModel()->selectedRows();
    sources.reserve(static_cast<size_t>(selected.size()));
    for (const auto& index : selected) {
        if (index.isValid()) {
            if (const auto* entry = m_nested_entry_model->summary_at(index); entry != nullptr && entry->has_source) {
                sources.push_back(make_usm_recovery_source(*entry));
            }
        }
    }
    return sources;
}

std::vector<UsmRecoverySource> MainWindow::current_preview_usm_recovery_sources() const {
    if (m_current_preview_entry && m_current_preview_entry->has_source) {
        return {make_usm_recovery_source(*m_current_preview_entry)};
    }
    if (m_file_view != nullptr && m_file_proxy != nullptr && m_file_model != nullptr &&
        m_file_view->currentIndex().isValid()) {
        const auto source = m_file_proxy->mapToSource(m_file_view->currentIndex());
        if (source.isValid()) {
            if (const auto* document = m_file_model->document_at(source.row()); document != nullptr) {
                return {make_usm_recovery_source(*document)};
            }
        }
    }
    return {};
}

std::vector<AdxRecoverySource> MainWindow::selected_file_adx_recovery_sources() const {
    std::vector<AdxRecoverySource> sources;
    if (m_file_view == nullptr || m_file_proxy == nullptr || m_file_model == nullptr || m_file_view->selectionModel() == nullptr) {
        return sources;
    }
    const auto selected = m_file_view->selectionModel()->selectedRows();
    sources.reserve(static_cast<size_t>(selected.size()));
    for (const auto& index : selected) {
        const auto source = m_file_proxy->mapToSource(index);
        if (source.isValid()) {
            if (const auto* document = m_file_model->document_at(source.row()); document != nullptr) {
                append_document_adx_recovery_sources(*document, sources);
            }
        }
    }
    return sources;
}

std::vector<AdxRecoverySource> MainWindow::all_file_adx_recovery_sources() const {
    std::vector<AdxRecoverySource> sources;
    if (m_file_model == nullptr) {
        return sources;
    }
    const auto rows = m_file_model->rowCount();
    sources.reserve(static_cast<size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        if (const auto* document = m_file_model->document_at(row); document != nullptr) {
            append_document_adx_recovery_sources(*document, sources);
        }
    }
    return sources;
}

std::vector<AdxRecoverySource> MainWindow::selected_entry_adx_recovery_sources() const {
    std::vector<AdxRecoverySource> sources;
    if (m_entry_view == nullptr || m_entry_proxy == nullptr || m_entry_model == nullptr || m_entry_view->selectionModel() == nullptr) {
        return sources;
    }
    const auto selected = m_entry_view->selectionModel()->selectedRows();
    sources.reserve(static_cast<size_t>(selected.size()));
    for (const auto& index : selected) {
        const auto source = m_entry_proxy->mapToSource(index);
        if (source.isValid()) {
            if (const auto* entry = m_entry_model->summary_at(source); entry != nullptr && entry->has_source) {
                sources.push_back(make_adx_recovery_source(*entry));
            }
        }
    }
    return sources;
}

std::vector<AdxRecoverySource> MainWindow::selected_nested_entry_adx_recovery_sources() const {
    std::vector<AdxRecoverySource> sources;
    if (m_nested_entry_view == nullptr || m_nested_entry_model == nullptr || m_nested_entry_view->selectionModel() == nullptr) {
        return sources;
    }
    const auto selected = m_nested_entry_view->selectionModel()->selectedRows();
    sources.reserve(static_cast<size_t>(selected.size()));
    for (const auto& index : selected) {
        if (index.isValid()) {
            if (const auto* entry = m_nested_entry_model->summary_at(index); entry != nullptr && entry->has_source) {
                sources.push_back(make_adx_recovery_source(*entry));
            }
        }
    }
    return sources;
}

std::vector<AdxRecoverySource> MainWindow::current_preview_adx_recovery_sources() const {
    if (m_current_preview_entry && m_current_preview_entry->has_source) {
        return {make_adx_recovery_source(*m_current_preview_entry)};
    }
    if (m_file_view != nullptr && m_file_proxy != nullptr && m_file_model != nullptr &&
        m_file_view->currentIndex().isValid()) {
        const auto source = m_file_proxy->mapToSource(m_file_view->currentIndex());
        if (source.isValid()) {
            if (const auto* document = m_file_model->document_at(source.row()); document != nullptr) {
                return {make_adx_recovery_source(*document)};
            }
        }
    }
    return {};
}

std::optional<AdxRecoveryKind> MainWindow::current_preview_adx_recovery_kind() const {
    if (m_current_preview_entry) {
        return adx_recovery_kind(*m_current_preview_entry);
    }
    if (m_file_view == nullptr || m_file_proxy == nullptr || m_file_model == nullptr ||
        !m_file_view->currentIndex().isValid()) {
        return std::nullopt;
    }
    const auto source = m_file_proxy->mapToSource(m_file_view->currentIndex());
    const auto* document = source.isValid() ? m_file_model->document_at(source.row()) : nullptr;
    return document == nullptr ? std::nullopt : adx_recovery_kind(*document);
}

void MainWindow::show_loaded_file_context_menu(const QPoint& position) {
    if (m_file_view == nullptr || m_file_proxy == nullptr) {
        return;
    }
    const auto index = m_file_view->indexAt(position);
    if (index.isValid() && m_file_view->selectionModel() != nullptr && !m_file_view->selectionModel()->isSelected(index)) {
        m_file_view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_file_view->setCurrentIndex(index);
    }

    QMenu menu(this);
    auto* open_editor = menu.addAction(QCoreApplication::translate("MainWindow.BrowserView", "Open in Editor"));
    const auto source_index = index.isValid() ? m_file_proxy->mapToSource(index) : QModelIndex{};
    const auto* selected_document = source_index.isValid() && m_file_model != nullptr
        ? m_file_model->document_at(source_index.row())
        : nullptr;
    const auto can_open_selected = std::ranges::any_of(
        m_file_view->selectionModel()->selectedRows(),
        [this](const QModelIndex& selected) {
            const auto source = m_file_proxy->mapToSource(selected);
            const auto* document = source.isValid() ? m_file_model->document_at(source.row()) : nullptr;
            return document != nullptr && supports_editor(*document);
        });
    open_editor->setEnabled(can_open_selected);
    auto* show_in_folder = menu.addAction(QCoreApplication::translate("MainWindow.BrowserView", "Show in Folder"));
    menu.addSeparator();
    auto* extract = menu.addAction(make_action_icon(ActionGlyph::Extract), QCoreApplication::translate("MainWindow.BrowserView", "Extract Selected"));
    auto* extract_raw = menu.addAction(make_action_icon(ActionGlyph::RawExtract), QCoreApplication::translate("MainWindow.BrowserView", "Extract Selected Raw"));
    auto* recover_hca_keys = menu.addAction(make_action_icon(ActionGlyph::RecoverKey), QCoreApplication::translate("MainWindow.BrowserView", "Recover HCA Keys"));
    recover_hca_keys->setVisible(!selected_file_recovery_sources().empty());
    auto* recover_usm_keys = menu.addAction(make_action_icon(ActionGlyph::RecoverKey), QCoreApplication::translate("MainWindow.BrowserView", "Recover USM Keys"));
    auto* recover_adx_keys = menu.addAction(make_action_icon(ActionGlyph::RecoverKey), QCoreApplication::translate("MainWindow.BrowserView", "Recover ADX Keys"));
    auto* recover_ahx_keys = menu.addAction(make_action_icon(ActionGlyph::RecoverKey), QCoreApplication::translate("MainWindow.BrowserView", "Recover AHX Keys"));
    auto aac_sources = selected_file_aac_recovery_sources();
    QAction* recover_aac_keys = nullptr;
    if (!aac_sources.empty()) {
        recover_aac_keys = menu.addAction(make_action_icon(ActionGlyph::RecoverKey), QCoreApplication::translate("MainWindow.BrowserView", "Recover AAC Keys"));
    }
    menu.addSeparator();
    auto* unload = menu.addAction(make_action_icon(ActionGlyph::Clear), QCoreApplication::translate("MainWindow.BrowserView", "Unload"));
    const auto chosen = menu.exec(m_file_view->viewport()->mapToGlobal(position));
    if (chosen == open_editor) {
        open_selected_files_in_editor();
    } else if (chosen == show_in_folder && index.isValid()) {
        reveal_in_file_manager(index.data(FileListModel::PathRole).toString());
    } else if (chosen == extract) {
        start_extraction(selected_file_targets(), ExtractionMode::Decoded);
    } else if (chosen == extract_raw) {
        start_extraction(selected_file_targets(), ExtractionMode::Raw);
    } else if (chosen == recover_hca_keys) {
        auto sources = selected_file_recovery_sources();
        const auto count = sources.size();
        start_hca_key_recovery(
            std::move(sources),
            QCoreApplication::translate("MainWindow.BrowserView", "%n selected loaded file(s)", nullptr, static_cast<int>(count)));
    } else if (chosen == recover_usm_keys) {
        auto sources = selected_file_usm_recovery_sources();
        const auto count = sources.size();
        start_usm_key_recovery(
            std::move(sources),
            QCoreApplication::translate("MainWindow.BrowserView", "%n selected loaded file(s)", nullptr, static_cast<int>(count)));
    } else if (chosen == recover_adx_keys || chosen == recover_ahx_keys) {
        auto sources = selected_file_adx_recovery_sources();
        const auto count = sources.size();
        start_adx_key_recovery(
            std::move(sources),
            chosen == recover_ahx_keys ? AdxRecoveryKind::Ahx : AdxRecoveryKind::Adx,
            QCoreApplication::translate("MainWindow.BrowserView", "%n selected loaded file(s)", nullptr, static_cast<int>(count)));
    } else if (recover_aac_keys != nullptr && chosen == recover_aac_keys) {
        const auto count = aac_sources.size();
        start_aac_key_recovery(
            std::move(aac_sources),
            QCoreApplication::translate("MainWindow.BrowserView", "%n selected ACB/AWB file(s)", nullptr, static_cast<int>(count)));
    } else if (chosen == unload) {
        unload_selected_files();
    }
}

void MainWindow::show_entry_context_menu(const QPoint& position) {
    if (m_entry_view == nullptr || m_entry_proxy == nullptr) {
        return;
    }
    const auto index = m_entry_view->indexAt(position);
    if (index.isValid() && m_entry_view->selectionModel() != nullptr && !m_entry_view->selectionModel()->isSelected(index)) {
        m_entry_view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_entry_view->setCurrentIndex(index);
    }

    QMenu menu(this);
    auto* open_editor = menu.addAction(QCoreApplication::translate("MainWindow.BrowserView", "Open in Editor"));
    auto* show_in_folder = menu.addAction(QCoreApplication::translate("MainWindow.BrowserView", "Show Source in Folder"));
    const auto source_index = index.isValid() ? m_entry_proxy->mapToSource(index) : QModelIndex{};
    const auto* summary = source_index.isValid() ? m_entry_model->summary_at(source_index) : nullptr;
    const auto can_open_selected = std::ranges::any_of(
        m_entry_view->selectionModel()->selectedRows(),
        [this](const QModelIndex& selected) {
            const auto source = m_entry_proxy->mapToSource(selected);
            const auto* selected_summary = source.isValid() ? m_entry_model->summary_at(source) : nullptr;
            return selected_summary != nullptr && selected_summary->has_source && supports_editor(*selected_summary);
        });
    open_editor->setEnabled(can_open_selected);
    show_in_folder->setEnabled(summary != nullptr && !summary->source_path.empty());
    menu.addSeparator();
    auto* extract = menu.addAction(make_action_icon(ActionGlyph::Extract), QCoreApplication::translate("MainWindow.BrowserView", "Extract Entry"));
    auto* extract_raw = menu.addAction(make_action_icon(ActionGlyph::RawExtract), QCoreApplication::translate("MainWindow.BrowserView", "Extract Entry Raw"));
    const bool selected_acb_cue = std::ranges::any_of(
        m_entry_view->selectionModel()->selectedRows(),
        [this](const QModelIndex& selected) {
            const auto source = m_entry_proxy->mapToSource(selected);
            const auto* selected_summary = source.isValid()
                ? m_entry_model->summary_at(source)
                : nullptr;
            return selected_summary != nullptr &&
                selected_summary->source_format == "ACB Cue";
        });
    extract->setEnabled(!selected_entry_targets().empty());
    extract_raw->setEnabled(!selected_acb_cue);
    auto* recover_hca_keys = menu.addAction(make_action_icon(ActionGlyph::RecoverKey), QCoreApplication::translate("MainWindow.BrowserView", "Recover HCA Keys"));
    recover_hca_keys->setVisible(!selected_entry_recovery_sources().empty());
    auto* recover_usm_keys = menu.addAction(make_action_icon(ActionGlyph::RecoverKey), QCoreApplication::translate("MainWindow.BrowserView", "Recover USM Keys"));
    auto* recover_adx_keys = menu.addAction(make_action_icon(ActionGlyph::RecoverKey), QCoreApplication::translate("MainWindow.BrowserView", "Recover ADX Keys"));
    auto* recover_ahx_keys = menu.addAction(make_action_icon(ActionGlyph::RecoverKey), QCoreApplication::translate("MainWindow.BrowserView", "Recover AHX Keys"));
    auto aac_sources = selected_entry_aac_recovery_sources();
    QAction* recover_aac_keys = nullptr;
    if (!aac_sources.empty()) {
        recover_aac_keys = menu.addAction(make_action_icon(ActionGlyph::RecoverKey), QCoreApplication::translate("MainWindow.BrowserView", "Recover AAC Keys"));
    }
    menu.addSeparator();
    auto* copy_cell = menu.addAction(QCoreApplication::translate("MainWindow.BrowserView", "Copy Cell"));
    auto* copy_row = menu.addAction(QCoreApplication::translate("MainWindow.BrowserView", "Copy Row"));
    const auto chosen = menu.exec(m_entry_view->viewport()->mapToGlobal(position));
    if (chosen == open_editor) {
        open_selected_entries_in_editor();
    } else if (chosen == show_in_folder && summary != nullptr) {
        reveal_in_file_manager(path_to_qstring(summary->source_path));
    } else if (chosen == extract) {
        start_extraction(selected_entry_targets(), ExtractionMode::Decoded);
    } else if (chosen == extract_raw) {
        start_extraction(selected_entry_targets(), ExtractionMode::Raw);
    } else if (chosen == recover_hca_keys) {
        auto sources = selected_entry_recovery_sources();
        const auto count = sources.size();
        start_hca_key_recovery(
            std::move(sources),
            QCoreApplication::translate("MainWindow.BrowserView", "%n selected entry(s)", nullptr, static_cast<int>(count)));
    } else if (chosen == recover_usm_keys) {
        auto sources = selected_entry_usm_recovery_sources();
        const auto count = sources.size();
        start_usm_key_recovery(
            std::move(sources),
            QCoreApplication::translate("MainWindow.BrowserView", "%n selected entry(s)", nullptr, static_cast<int>(count)));
    } else if (chosen == recover_adx_keys || chosen == recover_ahx_keys) {
        auto sources = selected_entry_adx_recovery_sources();
        const auto count = sources.size();
        start_adx_key_recovery(
            std::move(sources),
            chosen == recover_ahx_keys ? AdxRecoveryKind::Ahx : AdxRecoveryKind::Adx,
            QCoreApplication::translate("MainWindow.BrowserView", "%n selected entry(s)", nullptr, static_cast<int>(count)));
    } else if (recover_aac_keys != nullptr && chosen == recover_aac_keys) {
        const auto count = aac_sources.size();
        start_aac_key_recovery(
            std::move(aac_sources),
            QCoreApplication::translate("MainWindow.BrowserView", "%n selected AAC entry(s)", nullptr, static_cast<int>(count)));
    } else if (chosen == copy_cell && index.isValid()) {
        QApplication::clipboard()->setText(index.data(Qt::DisplayRole).toString());
    } else if (chosen == copy_row && index.isValid()) {
        QApplication::clipboard()->setText(copied_row_text(m_entry_proxy, index));
    }
}

void MainWindow::show_nested_entry_context_menu(const QPoint& position) {
    if (m_nested_entry_view == nullptr || m_nested_entry_model == nullptr) {
        return;
    }
    const auto index = m_nested_entry_view->indexAt(position);
    if (index.isValid() && m_nested_entry_view->selectionModel() != nullptr && !m_nested_entry_view->selectionModel()->isSelected(index)) {
        m_nested_entry_view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_nested_entry_view->setCurrentIndex(index);
    }

    QMenu menu(this);
    auto* open_editor = menu.addAction(QCoreApplication::translate("MainWindow.BrowserView", "Open in Editor"));
    auto* show_in_folder = menu.addAction(QCoreApplication::translate("MainWindow.BrowserView", "Show Source in Folder"));
    const auto* summary = index.isValid() ? m_nested_entry_model->summary_at(index) : nullptr;
    const auto can_open_selected = std::ranges::any_of(
        m_nested_entry_view->selectionModel()->selectedRows(),
        [this](const QModelIndex& selected) {
            const auto* selected_summary = m_nested_entry_model->summary_at(selected);
            return selected_summary != nullptr && selected_summary->has_source && supports_editor(*selected_summary);
        });
    open_editor->setEnabled(can_open_selected);
    show_in_folder->setEnabled(summary != nullptr && !summary->source_path.empty());
    menu.addSeparator();
    auto* extract = menu.addAction(make_action_icon(ActionGlyph::Extract), QCoreApplication::translate("MainWindow.BrowserView", "Extract Entry"));
    auto* extract_raw = menu.addAction(make_action_icon(ActionGlyph::RawExtract), QCoreApplication::translate("MainWindow.BrowserView", "Extract Entry Raw"));
    auto* recover_hca_keys = menu.addAction(make_action_icon(ActionGlyph::RecoverKey), QCoreApplication::translate("MainWindow.BrowserView", "Recover HCA Keys"));
    recover_hca_keys->setVisible(!selected_nested_entry_recovery_sources().empty());
    auto* recover_usm_keys = menu.addAction(make_action_icon(ActionGlyph::RecoverKey), QCoreApplication::translate("MainWindow.BrowserView", "Recover USM Keys"));
    auto* recover_adx_keys = menu.addAction(make_action_icon(ActionGlyph::RecoverKey), QCoreApplication::translate("MainWindow.BrowserView", "Recover ADX Keys"));
    auto* recover_ahx_keys = menu.addAction(make_action_icon(ActionGlyph::RecoverKey), QCoreApplication::translate("MainWindow.BrowserView", "Recover AHX Keys"));
    auto aac_sources = selected_nested_entry_aac_recovery_sources();
    QAction* recover_aac_keys = nullptr;
    if (!aac_sources.empty()) {
        recover_aac_keys = menu.addAction(make_action_icon(ActionGlyph::RecoverKey), QCoreApplication::translate("MainWindow.BrowserView", "Recover AAC Keys"));
    }
    menu.addSeparator();
    auto* copy_cell = menu.addAction(QCoreApplication::translate("MainWindow.BrowserView", "Copy Cell"));
    auto* copy_row = menu.addAction(QCoreApplication::translate("MainWindow.BrowserView", "Copy Row"));
    const auto chosen = menu.exec(m_nested_entry_view->viewport()->mapToGlobal(position));
    if (chosen == open_editor) {
        open_selected_nested_entries_in_editor();
    } else if (chosen == show_in_folder && summary != nullptr) {
        reveal_in_file_manager(path_to_qstring(summary->source_path));
    } else if (chosen == extract) {
        start_extraction(selected_nested_entry_targets(), ExtractionMode::Decoded);
    } else if (chosen == extract_raw) {
        start_extraction(selected_nested_entry_targets(), ExtractionMode::Raw);
    } else if (chosen == recover_hca_keys) {
        auto sources = selected_nested_entry_recovery_sources();
        const auto count = sources.size();
        start_hca_key_recovery(
            std::move(sources),
            QCoreApplication::translate("MainWindow.BrowserView", "%n selected entry(s)", nullptr, static_cast<int>(count)));
    } else if (chosen == recover_usm_keys) {
        auto sources = selected_nested_entry_usm_recovery_sources();
        const auto count = sources.size();
        start_usm_key_recovery(
            std::move(sources),
            QCoreApplication::translate("MainWindow.BrowserView", "%n selected entry(s)", nullptr, static_cast<int>(count)));
    } else if (chosen == recover_adx_keys || chosen == recover_ahx_keys) {
        auto sources = selected_nested_entry_adx_recovery_sources();
        const auto count = sources.size();
        start_adx_key_recovery(
            std::move(sources),
            chosen == recover_ahx_keys ? AdxRecoveryKind::Ahx : AdxRecoveryKind::Adx,
            QCoreApplication::translate("MainWindow.BrowserView", "%n selected entry(s)", nullptr, static_cast<int>(count)));
    } else if (recover_aac_keys != nullptr && chosen == recover_aac_keys) {
        const auto count = aac_sources.size();
        start_aac_key_recovery(
            std::move(aac_sources),
            QCoreApplication::translate("MainWindow.BrowserView", "%n selected AAC entry(s)", nullptr, static_cast<int>(count)));
    } else if (chosen == copy_cell && index.isValid()) {
        QApplication::clipboard()->setText(index.data(Qt::DisplayRole).toString());
    } else if (chosen == copy_row && index.isValid()) {
        QApplication::clipboard()->setText(copied_row_text(m_nested_entry_model, index));
    }
}

void MainWindow::update_file_sort() {
    if (m_file_proxy == nullptr || m_file_sort == nullptr) {
        return;
    }

    switch (m_file_sort->currentData().toInt()) {
    case 1:
        m_file_proxy->setSortRole(FileListModel::NameSortRole);
        m_file_proxy->sort(0, Qt::DescendingOrder);
        break;
    case 2:
        m_file_proxy->setSortRole(FileListModel::SizeSortRole);
        m_file_proxy->sort(0, Qt::AscendingOrder);
        break;
    case 3:
        m_file_proxy->setSortRole(FileListModel::SizeSortRole);
        m_file_proxy->sort(0, Qt::DescendingOrder);
        break;
    case 0:
    default:
        m_file_proxy->setSortRole(FileListModel::NameSortRole);
        m_file_proxy->sort(0, Qt::AscendingOrder);
        break;
    }
}

void MainWindow::update_file_list_status() {
    if (m_file_list_status == nullptr || m_file_model == nullptr || m_file_proxy == nullptr || m_file_view == nullptr) {
        return;
    }

    const auto total = m_file_model->rowCount();
    const auto visible = m_file_proxy->rowCount();
    const auto selected = m_file_view->selectionModel() == nullptr
        ? 0
        : m_file_view->selectionModel()->selectedRows().size();

    auto text = visible == total
        ? QCoreApplication::translate("MainWindow.BrowserView", "%1 loaded").arg(total)
        : QCoreApplication::translate("MainWindow.BrowserView", "%1 of %2 shown").arg(visible).arg(total);
    if (selected > 0) {
        text += QCoreApplication::translate("MainWindow.BrowserView", " · %1 selected").arg(selected);
    }
    if (selected == 1 && m_file_view->currentIndex().isValid()) {
        const auto source = m_file_proxy->mapToSource(m_file_view->currentIndex());
        if (const auto* document = m_file_model->document_at(source.row()); document != nullptr) {
            const auto path = path_to_qstring(document->path);
            const auto elided_path = m_file_list_status->fontMetrics().elidedText(
                path,
                Qt::ElideMiddle,
                std::max(120, m_file_list_status->width() - 150)
            );
            text += QStringLiteral(" · %1 · %2")
                .arg(QString::fromStdString(localized_document_format(*document)), elided_path);
            m_file_list_status->setToolTip(path);
        } else {
            m_file_list_status->setToolTip({});
        }
    } else {
        m_file_list_status->setToolTip({});
    }
    m_file_list_status->setText(text);
}

void MainWindow::update_entry_selection_status() {
    if (m_entry_selection_status == nullptr || m_entry_view == nullptr) {
        return;
    }

    const auto selected = m_entry_view->selectionModel() == nullptr
        ? 0
        : m_entry_view->selectionModel()->selectedRows().size();
    m_entry_selection_status->setText(
        selected == 1
            ? QCoreApplication::translate("MainWindow.BrowserView", "1 selected")
            : QCoreApplication::translate("MainWindow.BrowserView", "%1 selected").arg(selected)
    );
}

void MainWindow::set_entry_list_path(const QString& path) {
    if (m_entry_model == nullptr || m_entry_proxy == nullptr || m_entry_view == nullptr) {
        return;
    }

    m_entry_model->set_flat_path(path.toStdString());
    m_entry_proxy->invalidate();
    m_entry_view->scrollToTop();
    update_entry_path_bar();
    fit_entry_columns(m_entry_view, m_entry_model->has_custom_columns());
}

void MainWindow::update_entry_path_bar() {
    if (m_entry_path_row == nullptr || m_entry_path_label == nullptr || m_entry_model == nullptr) {
        return;
    }

    const auto visible = m_entry_model->flat_mode() && !m_entry_model->has_custom_columns();
    m_entry_path_row->setVisible(visible);
    if (!visible) {
        return;
    }

    const auto path = QString::fromStdString(m_entry_model->flat_path()).replace(QLatin1Char('\\'), QLatin1Char('/'));
    const auto link_color = QApplication::palette().color(QPalette::Mid).name();
    const auto link = [&link_color](const QString& target, const QString& label) {
        return QStringLiteral("<a style=\"color:%1; text-decoration:none\" href=\"%2\">%3</a>")
            .arg(link_color, target.toHtmlEscaped(), label.toHtmlEscaped());
    };
    QString html = link(QStringLiteral("@root"), QStringLiteral("Archive"));
    QString current;
    for (const auto& part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        current += current.isEmpty() ? part : QStringLiteral("/") + part;
        html += QStringLiteral(" / ") + link(current, part);
    }
    m_entry_path_label->setText(html);
    if (m_entry_up_button != nullptr) {
        m_entry_up_button->setEnabled(m_entry_model->flat_can_go_up());
        m_entry_up_button->setVisible(false);
    }
}

void MainWindow::activate_current_entry() {
    if (m_entry_view == nullptr || m_entry_proxy == nullptr || m_entry_model == nullptr) {
        return;
    }

    const auto current = m_entry_view->currentIndex();
    if (!current.isValid()) {
        return;
    }

    const auto source = m_entry_proxy->mapToSource(current);
    if (
        m_entry_model->flat_mode() &&
        source.data(EntryTableModel::IsFolderRole).toBool()
    ) {
        set_entry_list_path(source.data(EntryTableModel::FolderPathRole).toString());
        return;
    }

    const auto* summary = m_entry_model->summary_at(source);
    if (summary == nullptr) {
        return;
    }
    if (summary->source_format == "ACB Cue") {
        show_acb_cue(summary->source_index);
    } else if (summary->has_source) {
        start_entry_preview(*summary);
    } else if (!summary->inspector_entries.empty()) {
        show_entry_inspector(*summary);
    }
}

void MainWindow::set_preview_entry_actions_visible(bool visible) {
    const auto expanded = m_preview_panel_button == nullptr || m_preview_panel_button->isChecked();
    const auto show = visible && expanded;
    if (m_preview_extract_button != nullptr) {
        m_preview_extract_button->setVisible(show);
    }
    if (m_preview_extract_raw_button != nullptr) {
        m_preview_extract_raw_button->setVisible(
            show && !(m_showing_acb_cues &&
                      m_current_acb_cue_index.has_value()));
    }
    if (m_preview_recover_key_button != nullptr) {
        const bool supports_hca = expanded && !current_preview_recovery_sources().empty();
        m_preview_recover_key_button->setVisible(supports_hca);
        m_preview_recover_key_button->setEnabled(!key_recovery_running());
    }
    if (m_preview_recover_usm_key_button != nullptr) {
        bool supports_usm = m_current_preview_entry.has_value() &&
            is_usm_recovery_entry(*m_current_preview_entry);
        if (!m_current_preview_entry && m_file_view != nullptr && m_file_proxy != nullptr &&
            m_file_model != nullptr && m_file_view->currentIndex().isValid()) {
            const auto source = m_file_proxy->mapToSource(m_file_view->currentIndex());
            if (const auto* document = source.isValid() ? m_file_model->document_at(source.row()) : nullptr;
                document != nullptr) {
                const auto format = QString::fromStdString(document->loader_tag + " " + document->format).toLower();
                supports_usm = format == QStringLiteral("usm") || format.contains(QStringLiteral("usm/")) ||
                    format.startsWith(QStringLiteral("usm "));
            }
        }
        const auto expanded = m_preview_panel_button == nullptr || m_preview_panel_button->isChecked();
        m_preview_recover_usm_key_button->setVisible(expanded && supports_usm);
        m_preview_recover_usm_key_button->setEnabled(!key_recovery_running());
    }
    if (m_preview_recover_adx_key_button != nullptr) {
        const auto kind = current_preview_adx_recovery_kind();
        m_preview_recover_adx_key_button->setVisible(expanded && kind.has_value());
        m_preview_recover_adx_key_button->setEnabled(!key_recovery_running());
        if (kind) {
            const auto name = *kind == AdxRecoveryKind::Ahx ? QStringLiteral("AHX") : QStringLiteral("ADX");
            m_preview_recover_adx_key_button->setText(QCoreApplication::translate("MainWindow.BrowserView", "Recover %1 Key").arg(name));
            m_preview_recover_adx_key_button->setToolTip(
                QCoreApplication::translate("MainWindow.BrowserView", "Recover an effective %1 type-8/type-9 key triplet from this previewed file").arg(name));
        }
    }
    if (m_preview_recover_aac_key_button != nullptr) {
        bool supports_aac = m_current_preview_entry.has_value() &&
            supports_aac_key_recovery(*m_current_preview_entry);
        if (!m_current_preview_entry && m_file_view != nullptr && m_file_proxy != nullptr &&
            m_file_model != nullptr && m_file_view->currentIndex().isValid()) {
            const auto source = m_file_proxy->mapToSource(m_file_view->currentIndex());
            const auto* document = source.isValid() ? m_file_model->document_at(source.row()) : nullptr;
            supports_aac = document != nullptr && supports_aac_key_recovery(*document);
        }
        m_preview_recover_aac_key_button->setVisible(expanded && supports_aac);
        m_preview_recover_aac_key_button->setEnabled(!key_recovery_running());
    }
}

void MainWindow::fit_entry_columns(QTreeView* view, bool custom_columns) const {
    if (view == nullptr || view->model() == nullptr || view->model()->columnCount() <= 0) {
        return;
    }

    auto* header = view->header();
    if (header == nullptr) {
        return;
    }

    const auto column_count = view->model()->columnCount();
    const auto available = std::max(260, view->viewport()->width() - 8);
    std::vector<int> widths(static_cast<size_t>(column_count), 96);
    const auto assign = [&](int column, int width) {
        if (column >= 0 && column < column_count) {
            widths[static_cast<size_t>(column)] = std::max(36, width);
        }
    };

    if (column_count == 1) {
        widths[0] = available;
    } else if (column_count == 2) {
        assign(0, (available * 68) / 100);
        assign(1, available - widths[0]);
    } else if (column_count == 3) {
        assign(0, (available * 52) / 100);
        assign(1, (available * 20) / 100);
        assign(2, available - widths[0] - widths[1]);
    } else if (column_count == 4) {
        assign(0, (available * 42) / 100);
        assign(1, (available * 16) / 100);
        assign(2, (available * 19) / 100);
        assign(3, available - widths[0] - widths[1] - widths[2]);
    } else {
        assign(0, custom_columns ? (available * 32) / 100 : (available * 33) / 100);
        assign(1, (available * 12) / 100);
        assign(2, (available * 15) / 100);
        assign(3, (available * 12) / 100);
        auto used = std::accumulate(widths.begin(), widths.begin() + 4, 0);
        assign(4, std::max(36, available - used));
        used = std::accumulate(widths.begin(), widths.begin() + std::min(5, column_count), 0);
        if (column_count > 5) {
            const auto remaining = std::max(36, (available - used) / (column_count - 5));
            for (int column = 5; column < column_count; ++column) {
                assign(column, remaining);
            }
        }
    }

    header->setStretchLastSection(false);
    header->setSectionResizeMode(QHeaderView::Interactive);
    for (int column = 0; column < column_count; ++column) {
        view->setColumnWidth(column, widths[static_cast<size_t>(column)]);
    }
}

std::optional<int> MainWindow::current_mux_audio_choice() const {
    if (!m_media.audio_row->isVisible() || m_media.audio_combo->currentIndex() < 0) {
        return std::nullopt;
    }
    return m_media.audio_combo->currentData().toInt();
}



} // namespace cristudio
