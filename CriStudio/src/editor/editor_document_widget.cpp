#include "shared/i18n.hpp"
#include "editor/editor_document_widget.hpp"

#include "editor_workspace.hpp"

#include "editor/archive_editor_helpers.hpp"
#include "editor/editor_document_ui.hpp"
#include "editor/editor_helpers.hpp"
#include "editor/hex_patterns.hpp"
#include "editor/hex_preview_widget.hpp"
#include "editor/editor_session_loaders.hpp"
#include "editor/editor_widgets.hpp"
#include "editor/scratch_editor_session.hpp"
#include "editor/transform_detail_model.hpp"
#include "editor/transform_editor_helpers.hpp"
#include "entry_table_model.hpp"
#include "modules/aax/aax_edit.hpp"
#include "modules/acb/acb_edit.hpp"
#include "modules/acb/acb_edit_ui.hpp"
#include "modules/acx/acx_edit.hpp"
#include "modules/acx/acx_edit_ui.hpp"
#include "modules/afs/afs_edit.hpp"
#include "modules/afs/afs_edit_ui.hpp"
#include "modules/audio/audio_encode.hpp"
#include "modules/audio/audio_encode_ui.hpp"
#include "modules/adx/adx_container_build_ui.hpp"
#include "modules/adx/adx_edit.hpp"
#include "modules/adx/adx_edit_ui.hpp"
#include "modules/aix/aix_edit.hpp"
#include "modules/awb/awb_edit.hpp"
#include "modules/awb/awb_edit_ui.hpp"
#include "modules/csb/csb_edit.hpp"
#include "modules/csb/csb_edit_ui.hpp"
#include "modules/cpk/cpk_edit.hpp"
#include "modules/cpk/cpk_edit_ui.hpp"
#include "modules/cvm/cvm_edit.hpp"
#include "modules/cvm/cvm_edit_ui.hpp"
#include "modules/hca/hca_common.hpp"
#include "modules/hca/hca_edit.hpp"
#include "modules/hca/hca_edit_ui.hpp"
#include "modules/sfd/sfd_browse.hpp"
#include "modules/sfd/sfd_edit.hpp"
#include "modules/utf/utf_edit.hpp"
#include "modules/utf/utf_edit_ui.hpp"
#include "modules/usm/media_build.hpp"
#include "modules/usm/media_build_ui.hpp"
#include "modules/usm/usm_edit.hpp"
#include "modules/usm/usm_browse.hpp"
#include "main_window/preview_mux.hpp"
#include "main_window/preview_helpers.hpp"
#include "path_text.hpp"
#include "shared/document_preview_router.hpp"
#include "shared/document_sniffer.hpp"
#include "shared/embedded_document_loader.hpp"

#include "awb_entry_codec.hpp"
#include "utf_table.hpp"

#include "aax_container.hpp"
#include "acb_container.hpp"
#include "acx_container.hpp"
#include "adx_codec.hpp"
#include "afs_container.hpp"
#include "aix_container.hpp"
#include "awb_container.hpp"
#include "cpk_container.hpp"
#include "csb_container.hpp"
#include "cvm_container.hpp"
#include "hca_codec.hpp"
#include "sfd_container.hpp"
#include "usm_container.hpp"
#include "wav_container.hpp"

#include <QCoreApplication>
#include <QAbstractItemView>
#include <QApplication>
#include <QAudioOutput>
#include <QAction>
#include <QBasicTimer>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMediaPlayer>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTableView>
#include <QTextCursor>
#include <QTime>
#include <QTimeEdit>
#include <QTimer>
#include <QTimerEvent>
#include <QToolButton>
#include <QUrl>
#include <QVideoWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace cristudio {
namespace {

QString scratch_title(EditorOpenRequest::ScratchKind kind) {
    switch (kind) {
    case EditorOpenRequest::ScratchKind::Utf:
        return QCoreApplication::translate("Editor.EditorDocumentWidget", "New UTF Table");
    case EditorOpenRequest::ScratchKind::Afs:
        return QCoreApplication::translate("Editor.EditorDocumentWidget", "New AFS Archive");
    case EditorOpenRequest::ScratchKind::Awb:
        return QCoreApplication::translate("Editor.EditorDocumentWidget", "New AWB/AFS2 Archive");
    case EditorOpenRequest::ScratchKind::Acx:
        return QCoreApplication::translate("Editor.EditorDocumentWidget", "New ACX Archive");
    case EditorOpenRequest::ScratchKind::Cpk:
        return QCoreApplication::translate("Editor.EditorDocumentWidget", "New CPK Archive");
    case EditorOpenRequest::ScratchKind::AudioEncode:
        return QCoreApplication::translate("Editor.EditorDocumentWidget", "Encode Audio");
    case EditorOpenRequest::ScratchKind::MediaBuild:
        return QCoreApplication::translate("Editor.EditorDocumentWidget", "Build Movie");
    case EditorOpenRequest::ScratchKind::AaxBuild:
        return QCoreApplication::translate("Editor.EditorDocumentWidget", "Build AAX");
    case EditorOpenRequest::ScratchKind::AixBuild:
        return QCoreApplication::translate("Editor.EditorDocumentWidget", "Build AIX");
    case EditorOpenRequest::ScratchKind::CsbBuild:
        return QCoreApplication::translate("Editor.EditorDocumentWidget", "Build CSB");
    case EditorOpenRequest::ScratchKind::CvmScript:
    case EditorOpenRequest::ScratchKind::CvmDirectory:
    case EditorOpenRequest::ScratchKind::None:
        return {};
    }
    return {};
}

using modules::utf::utf_flag_from_name;
using modules::utf::parse_hex_bytes;
using modules::utf::parse_utf_value;
using modules::utf::cell_bytes;
using modules::utf::set_value;
using modules::utf::utf_table_preview;
using modules::utf::utf_type_from_name;
using modules::utf::utf_type_name;
using modules::utf::utf_value_text;

std::optional<std::string> direct_audio_format(
    std::span<const uint8_t> bytes,
    const EntrySummary* entry = nullptr
) {
    const auto codec = cricodecs::awb::probe_entry_codec(bytes);
    if (codec != cricodecs::awb::EntryCodec::Unknown) {
        return std::string(cricodecs::awb::entry_codec_name(codec));
    }
    const auto formats = entry == nullptr
        ? sniff_format_order(bytes)
        : sniff_embedded_format_order(
            bytes,
            entry->name,
            entry->type,
            entry->source_format,
            entry->nested_source_format
        );
    if (!formats.empty() && is_direct_audio_format(formats.front())) {
        return formats.front();
    }
    return std::nullopt;
}

struct EditorAudioPreviewRequest {
    uint64_t request_id = 0;
    LoadedDocument document;
    std::vector<uint8_t> bytes;
};

struct EditorAudioPreviewResult {
    uint64_t request_id = 0;
    std::expected<AudioPreview, std::string> preview;
};

struct EditorVideoPreviewResult {
    uint64_t request_id = 0;
    VideoPreview preview;
};

struct EditorMuxPreviewResult {
    uint64_t request_id = 0;
    std::expected<MuxPreview, std::string> preview;
};

class EditorDocumentWidget final : public QWidget {
public:
    explicit EditorDocumentWidget(EditorOpenRequest request, QTabWidget* tabs)
        : QWidget(tabs),
          m_request(std::move(request)),
          m_tabs(tabs) {
        build_ui();
        load_request();
    }

    ~EditorDocumentWidget() override {
        if (m_editor_media_player != nullptr) {
            m_editor_media_player->stop();
        }
        if (!m_editor_preview_temp_dir.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(m_editor_preview_temp_dir, ec);
        }
    }

    [[nodiscard]] QString tab_title() const {
        const auto marker = m_dirty ? QStringLiteral("*") : QString{};
        return marker + m_title;
    }

    [[nodiscard]] bool is_dirty() const noexcept { return m_dirty; }
    [[nodiscard]] bool has_background_work() const noexcept { return save_running(); }

    void retranslate() {
        retranslate_editor_document_ui(m_ui);
        const bool playing = m_editor_media_player != nullptr &&
            m_editor_media_player->playbackState() == QMediaPlayer::PlayingState;
        m_ui.media.play_button->setText(playing
            ? QCoreApplication::translate("Editor.EditorDocumentWidget", "Pause")
            : QCoreApplication::translate("Editor.EditorDocumentWidget", "Play"));
        m_ui.table_model->retranslate();
        m_ui.field_model->retranslate();
        m_ui.transform_model->retranslate();

        if (const auto title = scratch_title(m_request.scratch_kind); !title.isEmpty()) {
            m_title = title;
        }
        m_ui.title_label->setText(m_title);
        m_ui.subtitle_label->setText(editor_format_label(m_request));
        populate_info();
        sync_local_cri_key_ui();

        if (m_archive_kind != ArchiveKind::None) {
            const auto selected_row = m_ui.archive_table->currentRow();
            const QSignalBlocker selection_blocker(m_ui.archive_table);
            refresh_archive_document_ui(
                m_ui,
                archive_view(),
                m_request.keys
            );
            if (selected_row >= 0 && selected_row < m_ui.archive_table->rowCount()) {
                m_ui.archive_table->setCurrentCell(selected_row, 0);
            }
        } else if (m_transform_kind != TransformKind::None) {
            const auto selected_row = m_ui.transform_table->currentIndex().row();
            const QSignalBlocker selection_blocker(m_ui.transform_table->selectionModel());
            m_transform_rows = transform_detail_rows(m_transform_kind, transform_view());
            m_usm_chunk_rows = m_transform_kind == TransformKind::Usm && m_usm
                ? modules::usm::chunk_detail_rows(*m_usm)
                : std::vector<modules::TransformDetailRow>{};
            refresh_visible_transform_rows();
            refresh_transform_document_ui(
                m_ui,
                m_transform_kind,
                transform_view(),
                m_visible_transform_rows,
                m_ui.transform_filter_edit->text()
            );
            if (selected_row >= 0 && selected_row < m_ui.transform_model->rowCount()) {
                m_ui.transform_table->selectRow(selected_row);
            }
        } else if (m_utf) {
            refresh_utf_view();
        }

        update_header_actions();
        refresh_title();
    }

    [[nodiscard]] bool confirm_close(QWidget* parent) {
        if (!m_dirty) {
            return true;
        }
        if (save_running()) {
            QMessageBox::information(
                parent,
                QCoreApplication::translate("Editor.EditorDocumentWidget", "Editor tab is busy"),
                QCoreApplication::translate("Editor.EditorDocumentWidget", "\"%1\" is already saving or building. Close it after the current job finishes.").arg(m_title)
            );
            return false;
        }
        const auto choice = QMessageBox::warning(
            parent,
            QCoreApplication::translate("Editor.EditorDocumentWidget", "Close dirty editor tab"),
            QCoreApplication::translate("Editor.EditorDocumentWidget", "\"%1\" has unsaved editor changes. Save before closing?").arg(m_title),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save
        );
        if (choice == QMessageBox::Discard) {
            return true;
        }
        if (choice == QMessageBox::Save) {
            return save_for_close(parent);
        }
        return false;
    }

protected:
    void hideEvent(QHideEvent* event) override {
        dismiss_editor_media_preview();
        QWidget::hideEvent(event);
    }

private:

    [[nodiscard]] bool save_running() const noexcept {
        return m_save_future.valid();
    }

    void poll_background_work() {
        for (const auto& line : take_job_logs(m_active_job_log)) {
            append_log(line);
        }
        if (!save_running()) {
            return;
        }
        if (m_save_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }

        auto result = m_save_future.get();
        for (const auto& line : take_job_logs(m_active_job_log)) {
            append_log(line);
        }
        m_active_job_log.reset();
        update_header_actions();
        m_ui.progress->hide();
        const bool saves_document = m_active_job_saves_document;
        const auto job_label = std::exchange(m_active_job_label, {});
        m_active_job_saves_document = false;
        if (result) {
            bool applied_job_output = false;
            if (m_apply_job_output_path) {
                const auto path = std::move(*m_apply_job_output_path);
                m_apply_job_output_path.reset();
                applied_job_output = apply_generated_output(path);
            }
            if (saves_document && !applied_job_output) {
                m_dirty = false;
            }
            append_log(saves_document
                ? QCoreApplication::translate("Editor.EditorDocumentWidget", "Saved: %1").arg(path_to_qstring(m_last_save_path))
                : QCoreApplication::translate("Editor.EditorDocumentWidget", "%1 completed: %2").arg(job_label, path_to_qstring(m_last_save_path)));
            refresh_title();
            if (saves_document && m_close_after_save) {
                close_tab();
            }
        } else {
            m_apply_job_output_path.reset();
            m_close_after_save = false;
            const auto failure_title = saves_document ? QCoreApplication::translate("Editor.EditorDocumentWidget", "Save failed") : job_label + QStringLiteral(" failed");
            append_log(QStringLiteral("%1: %2").arg(failure_title, result.error()));
            QMessageBox::warning(this, failure_title, result.error());
        }
    }

    bool apply_generated_output(const std::filesystem::path& path) {
        auto bytes = read_file_bytes(path);
        if (!bytes) {
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Job completed, but loading its output failed: %1").arg(bytes.error()));
            return false;
        }

        dismiss_editor_media_preview();
        m_bytes = std::move(*bytes);
        m_document.reset();
        m_utf.reset();
        m_afs.reset();
        m_awb.reset();
        m_acx.reset();
        m_cpk.reset();
        m_cvm.reset();
        m_adx.reset();
        m_hca.reset();
        m_aax.reset();
        m_aix.reset();
        m_usm.reset();
        m_sfd.reset();
        m_csb.reset();
        m_acb.reset();
        m_archive_kind = ArchiveKind::None;
        m_transform_kind = TransformKind::None;
        m_cpk_obfuscate_utf = false;
        m_cvm_scramble_key.clear();

        m_request.source_kind = EditorOpenRequest::SourceKind::Path;
        m_request.display_name = filename_to_utf8(path);
        m_request.detected_format.clear();
        m_request.source_path = path;
        m_request.source_archive_path.clear();
        m_request.source_archive_format.clear();
        m_request.source_index = 0;
        m_request.document.reset();
        m_request.entry.reset();
        m_request.source_bytes.reset();

        std::string reason;
        m_document = load_document_summary(path, reason, m_request.keys);
        if (m_document) {
            m_request.detected_format = m_document->loader_tag.empty()
                ? m_document->format
                : m_document->loader_tag;
        } else if (!reason.empty()) {
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Generated output summary unavailable: %1")
                .arg(utf8_to_qstring(reason)));
        }
        try_load_transform(&path);
        try_load_utf();
        try_load_archive();

        m_title = editor_label(m_request.display_name);
        m_ui.title_label->setText(m_title);
        m_ui.subtitle_label->setText(editor_format_label(m_request));
        m_last_save_file_path = path;
        m_dirty = false;
        refresh_summary();
        sync_local_cri_key_ui();
        update_header_actions();
        refresh_title();
        append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Loaded generated output into this editor session: %1")
            .arg(path_to_qstring(path)));

        if (m_transform_kind == TransformKind::Usm || m_transform_kind == TransformKind::Sfd) {
            QTimer::singleShot(0, this, [this] { preview_current_mux(); });
        } else if (m_document && is_direct_audio_format(m_document->format)) {
            QTimer::singleShot(0, this, [this] { preview_current_audio(); });
        }
        return true;
    }

    void set_preview_tabs(bool preview_available, bool raw_available, int preferred_tab) {
        m_ui.preview_tabs->setTabEnabled(0, preview_available);
        m_ui.preview_tabs->setTabEnabled(1, raw_available);
        if (preferred_tab == 0 && preview_available) {
            m_ui.preview_tabs->setCurrentIndex(0);
        } else if (raw_available) {
            m_ui.preview_tabs->setCurrentIndex(1);
        } else if (preview_available) {
            m_ui.preview_tabs->setCurrentIndex(0);
        }
    }

    [[nodiscard]] bool raw_preview_available() const noexcept {
        return !m_ui.hex_preview->isHidden();
    }

    void set_raw_preview(std::span<const uint8_t> bytes, uint64_t total_size, std::string_view format = {}) {
        if (bytes.empty()) {
            m_ui.hex_preview->clear_bytes();
            m_ui.hex_preview->hide();
            return;
        }
        const auto full_size = total_size == 0 ? bytes.size() : total_size;
        const auto prefix = bytes.first(std::min(bytes.size(), HexPreviewWidget::buffered_byte_limit));
        const auto effective_format = format.empty() && m_document ? std::string_view(m_document->format) : format;
        m_ui.hex_preview->set_source(prefix, full_size, effective_format);
        m_ui.hex_preview->show();
    }

    void clear_raw_preview() {
        m_preview_scratch.clear();
        m_ui.hex_preview->clear_bytes();
        m_ui.hex_preview->hide();
    }

    void set_raw_preview(const EntrySummary& summary, std::span<const uint8_t> bytes, uint64_t total_size) {
        if (bytes.empty()) {
            set_raw_preview({}, 0);
            return;
        }
        const auto full_size = total_size == 0 ? bytes.size() : total_size;
        const auto prefix = bytes.first(std::min(bytes.size(), HexPreviewWidget::buffered_byte_limit));
        m_ui.hex_preview->set_source(prefix, full_size, summary);
        m_ui.hex_preview->show();
    }

    void show_detail_preview(QString text) {
        dismiss_editor_media_preview();
        clear_raw_preview();
        const auto lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        constexpr qsizetype max_detail_rows = 512;
        const auto visible_lines = (std::min)(lines.size(), max_detail_rows);
        const auto omitted_lines = lines.size() - visible_lines;
        m_ui.payload_table->clear();
        m_ui.payload_table->setColumnCount(2);
        m_ui.payload_table->setHorizontalHeaderLabels({QCoreApplication::translate("Editor.EditorDocumentWidget", "Field"), QCoreApplication::translate("Editor.EditorDocumentWidget", "Value")});
        m_ui.payload_table->setRowCount(visible_lines + (omitted_lines > 0 ? 1 : 0));
        for (qsizetype row = 0; row < visible_lines; ++row) {
            const auto& line = lines[row];
            const auto separator = line.indexOf(QLatin1Char(':'));
            const auto bracket = line.startsWith(QLatin1Char('[')) ? line.indexOf(QLatin1Char(']')) : -1;
            const auto field = separator > 0
                ? line.left(separator).trimmed()
                : bracket > 0 ? line.left(bracket + 1).trimmed() : QStringLiteral("Detail");
            const auto value = separator > 0
                ? line.mid(separator + 1).trimmed()
                : bracket > 0 ? line.mid(bracket + 1).trimmed() : line;
            auto* field_item = new QTableWidgetItem(field);
            field_item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            auto* value_item = new QTableWidgetItem(value);
            value_item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            m_ui.payload_table->setItem(static_cast<int>(row), 0, field_item);
            m_ui.payload_table->setItem(static_cast<int>(row), 1, value_item);
        }
        if (omitted_lines > 0) {
            auto* field_item = new QTableWidgetItem(QCoreApplication::translate("Editor.EditorDocumentWidget", "More rows"));
            field_item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            auto* value_item = new QTableWidgetItem(QCoreApplication::translate("Editor.EditorDocumentWidget", "%1 additional rows omitted").arg(omitted_lines));
            value_item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            m_ui.payload_table->setItem(static_cast<int>(visible_lines), 0, field_item);
            m_ui.payload_table->setItem(static_cast<int>(visible_lines), 1, value_item);
        }
        m_ui.payload_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_ui.payload_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_ui.payload_table->show();
        set_preview_tabs(true, false, 0);
    }

    void show_document_preview(
        const LoadedDocument& document,
        std::span<const uint8_t> bytes
    ) {
        dismiss_editor_media_preview();
        set_raw_preview(bytes, bytes.size(), std::string(document_format_id(document)));
        constexpr size_t max_rows = 1024;
        m_ui.payload_table->clear();
        if (!document.entries.empty()) {
            const auto columns = document.entry_columns.empty()
                ? std::vector<std::string>{"Name", cristudio::i18n::translate_utf8("Editor.EditorDocumentWidget", "Type"), "Size", "Offset", "Detail"}
                : document.entry_columns;
            const auto shown = std::min(document.entries.size(), max_rows);
            m_ui.payload_table->setColumnCount(static_cast<int>(columns.size()));
            QStringList headers;
            headers.reserve(static_cast<qsizetype>(columns.size()));
            for (const auto& column : columns) {
                headers.push_back(utf8_to_qstring(column));
            }
            m_ui.payload_table->setHorizontalHeaderLabels(headers);
            m_ui.payload_table->setRowCount(static_cast<int>(shown));
            for (size_t row = 0; row < shown; ++row) {
                const auto& entry = document.entries[row];
                const auto fallback = std::array<std::string_view, 5>{
                    entry.name, entry.type, entry.size, entry.offset, entry.detail
                };
                for (size_t column = 0; column < columns.size(); ++column) {
                    const auto value = column < entry.cells.size()
                        ? std::string_view(entry.cells[column])
                        : column < fallback.size() ? fallback[column] : std::string_view{};
                    auto* item = new QTableWidgetItem(utf8_to_qstring(value));
                    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                    m_ui.payload_table->setItem(static_cast<int>(row), static_cast<int>(column), item);
                }
            }
        } else {
            const auto shown = std::min(document.info.size(), max_rows);
            m_ui.payload_table->setColumnCount(2);
            m_ui.payload_table->setHorizontalHeaderLabels({QCoreApplication::translate("Editor.EditorDocumentWidget", "Field"), QCoreApplication::translate("Editor.EditorDocumentWidget", "Value")});
            m_ui.payload_table->setRowCount(static_cast<int>(shown));
            for (size_t row = 0; row < shown; ++row) {
                auto* field = new QTableWidgetItem(utf8_to_qstring(document.info[row].name));
                auto* value = new QTableWidgetItem(utf8_to_qstring(document.info[row].value));
                field->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                value->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                m_ui.payload_table->setItem(static_cast<int>(row), 0, field);
                m_ui.payload_table->setItem(static_cast<int>(row), 1, value);
            }
        }
        m_ui.payload_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        if (m_ui.payload_table->columnCount() > 0) {
            m_ui.payload_table->horizontalHeader()->setSectionResizeMode(
                m_ui.payload_table->columnCount() - 1,
                QHeaderView::Stretch);
        }
        m_ui.payload_table->show();
        set_preview_tabs(true, raw_preview_available(), 0);
    }

    void preview_usm_stream(int index, const modules::TransformDetailRow& detail) {
        if (!m_usm || index < 0 || index >= static_cast<int>(m_usm->streams().size())) {
            show_detail_preview(QCoreApplication::translate("Editor.EditorDocumentWidget", "USM stream preview failed: index out of range"));
            return;
        }
        auto bytes = m_usm->extract_stream(static_cast<uint32_t>(index));
        if (!bytes) {
            show_detail_preview(QCoreApplication::translate("Editor.EditorDocumentWidget", "USM stream preview failed: %1")
                .arg(utf8_to_qstring(bytes.error())));
            return;
        }

        const auto& stream = m_usm->streams()[static_cast<size_t>(index)];
        EntrySummary entry;
        if (m_document && index < static_cast<int>(m_document->entries.size())) {
            entry = m_document->entries[static_cast<size_t>(index)];
        } else {
            entry.name = qstring_to_utf8(detail.field);
            entry.source_format = "USM";
            entry.source_index = static_cast<uint32_t>(index);
        }

        switch (stream.stream_id) {
        case cricodecs::usm::UsmChunkType::SFA:
        case cricodecs::usm::UsmChunkType::AHX:
            if (auto format = direct_audio_format(*bytes, &entry)) {
                LoadedDocument document;
                document.display_name = entry.name;
                document.format = std::move(*format);
                start_audio_preview(std::move(document), std::move(*bytes));
                return;
            }
            break;
        case cricodecs::usm::UsmChunkType::SFV:
        case cricodecs::usm::UsmChunkType::ALP:
            if (auto video = video_preview_from_bytes(entry, *bytes)) {
                start_video_preview(std::move(*video));
                return;
            }
            break;
        case cricodecs::usm::UsmChunkType::SBT: {
            std::string reason;
            auto document = modules::usm::summarize_sbt_subtitles(
                path_from_utf8_lossy(entry.name), *bytes, reason);
            if (reason.empty()) {
                show_document_preview(document, *bytes);
                return;
            }
            break;
        }
        default: {
            std::string reason;
            if (auto document = summarize_embedded_bytes(entry, *bytes, reason)) {
                show_document_preview(*document, *bytes);
                return;
            }
            break;
        }
        }
        show_hex_preview(*bytes, bytes->size(), "USM");
    }

    void show_hex_preview(std::span<const uint8_t> bytes, uint64_t total_size = 0, std::string_view format = {}) {
        dismiss_editor_media_preview();
        m_ui.payload_table->hide();
        set_raw_preview(bytes, total_size, format);
        set_preview_tabs(false, raw_preview_available(), 1);
    }

    void show_hex_preview(const EntrySummary& summary, std::span<const uint8_t> bytes, uint64_t total_size = 0) {
        dismiss_editor_media_preview();
        m_ui.payload_table->hide();
        set_raw_preview(summary, bytes, total_size);
        set_preview_tabs(false, raw_preview_available(), 1);
    }

    void build_ui() {
        m_ui = build_editor_document_ui(this);

        connect(m_ui.save_button, &QToolButton::clicked, this, [this] { save(); });
        connect(m_ui.save_as_button, &QToolButton::clicked, this, [this] { save_as(); });
        connect(m_ui.build_button, &QToolButton::clicked, this, [this] { build_session(); });
        connect(m_ui.extract_button, &QToolButton::clicked, this, [this] { extract_copy(); });

        connect(m_ui.apply_table_name_button, &QPushButton::clicked, this, [this] { apply_table_name(); });
        connect(m_ui.add_row_button, &QPushButton::clicked, this, [this] { add_utf_row(); });
        connect(m_ui.remove_row_button, &QPushButton::clicked, this, [this] { remove_selected_utf_row(); });
        connect(m_ui.add_column_button, &QPushButton::clicked, this, [this] { add_utf_column(); });
        connect(m_ui.remove_column_button, &QPushButton::clicked, this, [this] { remove_selected_utf_column(); });

        connect(m_ui.add_archive_file_button, &QPushButton::clicked, this, [this] { add_archive_file(); });
        connect(m_ui.replace_archive_file_button, &QPushButton::clicked, this, [this] { replace_selected_archive_file(); });
        connect(m_ui.remove_archive_file_button, &QPushButton::clicked, this, [this] { remove_selected_archive_file(); });
        connect(m_ui.move_archive_entry_up_button, &QPushButton::clicked, this, [this] { move_selected_archive_entry(-1); });
        connect(m_ui.move_archive_entry_down_button, &QPushButton::clicked, this, [this] { move_selected_archive_entry(1); });
        connect(m_ui.rename_archive_entry_button, &QPushButton::clicked, this, [this] { rename_selected_archive_entry(); });
        connect(m_ui.reserve_afs_id_button, &QPushButton::clicked, this, [this] { reserve_afs_file_id(); });
        connect(m_ui.set_afs_timestamp_button, &QPushButton::clicked, this, [this] { set_selected_afs_timestamp(); });
        connect(m_ui.set_archive_wave_id_button, &QPushButton::clicked, this, [this] { set_selected_archive_wave_id(); });
        connect(m_ui.batch_awb_wave_ids_button, &QPushButton::clicked, this, [this] { assign_awb_wave_ids(); });
        connect(m_ui.archive_entry_options_button, &QPushButton::clicked, this, [this] { edit_archive_entry_properties(); });
        connect(m_ui.archive_options_button, &QPushButton::clicked, this, [this] { edit_archive_options(); });
        connect(m_ui.archive_compress_all_action, &QAction::triggered, this, [this] { set_all_cpk_compression(true); });
        connect(m_ui.archive_store_all_action, &QAction::triggered, this, [this] { set_all_cpk_compression(false); });
        connect(m_ui.import_afs_als_button, &QPushButton::clicked, this, [this] { import_afs_als_script(); });
        connect(m_ui.export_afs_header_button, &QPushButton::clicked, this, [this] { export_afs_file_id_header(); });
        connect(m_ui.import_cvm_script_button, &QPushButton::clicked, this, [this] { import_cvm_script(); });
        connect(m_ui.export_cvm_script_button, &QPushButton::clicked, this, [this] { export_cvm_script(); });
        connect(m_ui.extract_archive_entry_button, &QPushButton::clicked, this, [this] { extract_selected_archive_entry(false); });
        connect(m_ui.extract_raw_archive_entry_button, &QPushButton::clicked, this, [this] { extract_selected_archive_entry(true); });

        connect(m_ui.encode_transform_button, &QPushButton::clicked, this, [this] { open_audio_encode_wizard(); });
        connect(m_ui.decode_transform_button, &QPushButton::clicked, this, [this] { decode_transform_to_wav(); });
        connect(m_ui.decrypt_transform_button, &QPushButton::clicked, this, [this] { decrypt_transform_to_file(); });
        connect(m_ui.encrypt_transform_button, &QPushButton::clicked, this, [this] { encrypt_transform_to_file(); });
        connect(m_ui.rebuild_transform_button, &QPushButton::clicked, this, [this] { rebuild_transform_to_file(); });
        connect(m_ui.transform_options_button, &QPushButton::clicked, this, [this] { edit_transform_options(); });
        connect(m_ui.extract_transform_button, &QPushButton::clicked, this, [this] { extract_transform_payloads(); });
        connect(m_ui.adx_container_build_button, &QPushButton::clicked, this, [this] { open_adx_container_build_wizard(); });
        connect(m_ui.csb_directory_build_button, &QPushButton::clicked, this, [this] { open_csb_directory_build_wizard(); });
        connect(m_ui.media_build_wizard_button, &QPushButton::clicked, this, [this] {
            static_cast<void>(open_media_build_wizard());
        });
        connect(m_ui.editor_mux_preview_button, &QPushButton::clicked, this, [this] { preview_current_media(); });
        connect(m_ui.open_acb_awb_button, &QPushButton::clicked, this, [this] { open_acb_associated_awb(); });
        connect(m_ui.export_acb_awb_button, &QPushButton::clicked, this, [this] { export_acb_associated_awb(); });
        connect(m_ui.add_transform_entry_button, &QPushButton::clicked, this, [this] { add_transform_entries(); });
        connect(m_ui.replace_transform_entry_button, &QPushButton::clicked, this, [this] { replace_transform_entry(); });
        connect(m_ui.remove_transform_entry_button, &QPushButton::clicked, this, [this] { remove_transform_entry(); });
        connect(m_ui.move_transform_entry_up_button, &QPushButton::clicked, this, [this] { move_transform_entry(-1); });
        connect(m_ui.move_transform_entry_down_button, &QPushButton::clicked, this, [this] { move_transform_entry(1); });
        connect(m_ui.rename_transform_entry_button, &QPushButton::clicked, this, [this] { rename_transform_entry(); });
        connect(m_ui.toggle_transform_entry_flag_button, &QPushButton::clicked, this, [this] { toggle_transform_entry_flag(); });
        connect(m_ui.apply_cri_key_button, &QPushButton::clicked, this, [this] { apply_local_cri_key(); });
        connect(m_ui.cri_key_edit, &QLineEdit::returnPressed, this, [this] { apply_local_cri_key(); });
        connect(m_ui.cri_key_base, &QComboBox::currentIndexChanged, this, [this] { sync_local_cri_key_ui(); });
        connect(m_ui.local_key_type, &QComboBox::currentIndexChanged, this, [this] {
            sync_local_key_mode_controls();
        });
        connect(m_ui.cvm_scramble_check, &QAbstractButton::toggled, this, [this](bool enabled) {
            if (m_archive_kind == ArchiveKind::Cvm) {
                m_ui.cri_key_edit->setEnabled(enabled);
            }
        });

        connect(m_ui.apply_value_button, &QPushButton::clicked, this, [this] { apply_selected_utf_value(); });
        connect(m_ui.value_edit, &QLineEdit::returnPressed, this, [this] { apply_selected_utf_value(); });
        connect(m_ui.rename_column_button, &QPushButton::clicked, this, [this] { rename_selected_utf_column(); });
        connect(m_ui.replace_binary_button, &QPushButton::clicked, this, [this] { replace_selected_binary(); });

        connect(m_ui.table->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
            show_selected_entry(current);
        });
        connect(m_ui.field_table->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
            show_selected_field(current);
        });
        connect(m_ui.utf_grid, &QTableWidget::currentCellChanged, this, [this](int row, int column) {
            const auto [source_row, source_column] = utf_source_cell(row, column);
            show_selected_utf_cell(source_row, source_column);
        });
        connect(m_ui.utf_grid, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
            handle_utf_grid_item_changed(item);
        });
        connect(m_ui.schema_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
            handle_schema_item_changed(item);
        });
        connect(m_ui.archive_table, &QTableWidget::currentCellChanged, this, [this](int row, int) {
            show_selected_archive_entry(row);
        });
        connect(m_ui.archive_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
            handle_archive_item_changed(item);
        });
        connect(m_ui.transform_table->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                show_selected_transform_row(current.row());
        });
        connect(m_ui.transform_filter_edit, &QLineEdit::textChanged, this, [this](const QString&) {
            if (m_transform_kind == TransformKind::None) {
                return;
            }
            refresh_transform_rows_ui();
        });
        connect(m_ui.preview_tabs, &QTabWidget::currentChanged, this, [this](int index) {
            if (index != 0 || m_editor_media_player == nullptr || m_editor_media_player->source().isEmpty()) {
                return;
            }
            m_ui.mux_preview_panel->show();
            m_ui.media.panel->show();
        });
        connect(m_ui.media.play_button, &QToolButton::clicked, this, [this] {
            if (m_editor_media_player == nullptr) {
                return;
            }
            if (m_editor_media_player->playbackState() == QMediaPlayer::PlayingState) {
                m_editor_media_player->pause();
                m_ui.media.play_button->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Play"));
            } else {
                m_editor_media_player->play();
                m_ui.media.play_button->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Pause"));
            }
        });
        connect(m_ui.media.seek_slider, &QSlider::sliderPressed, this, [this] {
            m_editor_slider_dragging = true;
            m_editor_resume_after_seek = m_editor_media_player != nullptr &&
                m_editor_media_player->playbackState() == QMediaPlayer::PlayingState;
            if (m_editor_resume_after_seek) {
                m_editor_media_player->pause();
            }
        });
        connect(m_ui.media.seek_slider, &QSlider::sliderReleased, this, [this] {
            m_editor_slider_dragging = false;
            if (m_editor_media_player != nullptr) {
                m_editor_media_player->setPosition(m_ui.media.seek_slider->value());
                if (m_editor_resume_after_seek) {
                    m_editor_media_player->play();
                }
            }
            m_editor_resume_after_seek = false;
            update_editor_media_time();
        });
        connect(m_ui.media.seek_slider, &QSlider::sliderMoved, this, [this](int) {
            update_editor_media_time();
        });
        connect(m_ui.media.volume_slider, &QSlider::valueChanged, this, [this](int value) {
            if (m_editor_audio_output != nullptr) {
                m_editor_audio_output->setVolume(static_cast<float>(std::clamp(value, 0, 100)) / 100.0f);
            }
        });
        connect(m_ui.media.audio_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
            if (index >= 0 && (m_transform_kind == TransformKind::Usm || m_transform_kind == TransformKind::Sfd)) {
                preview_current_mux(m_ui.media.audio_combo->currentData().toInt());
            }
        });
        connect(m_ui.media.subtitle_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
            if (index >= 0 && m_editor_media_player != nullptr) {
                m_editor_media_player->setActiveSubtitleTrack(m_ui.media.subtitle_combo->currentData().toInt());
            }
        });
    }

    void load_request() {
        m_title = editor_label(m_request.display_name);
        m_ui.title_label->setText(m_title);
        m_ui.subtitle_label->setText(editor_format_label(m_request));

        switch (m_request.source_kind) {
        case EditorOpenRequest::SourceKind::Path:
            load_path_request();
            break;
        case EditorOpenRequest::SourceKind::ArchiveEntry:
            load_archive_entry_request();
            break;
        case EditorOpenRequest::SourceKind::Scratch:
            load_scratch_request();
            break;
        }

        refresh_summary();
        sync_local_cri_key_ui();
        update_header_actions();
        refresh_title();
        if (m_transform_kind == TransformKind::MediaBuild) {
            QTimer::singleShot(0, this, [this] {
                if (!open_media_build_wizard()) {
                    close_tab();
                }
            });
        } else if (m_transform_kind == TransformKind::None && m_document && is_direct_audio_format(m_document->format)) {
            QTimer::singleShot(0, this, [this] { preview_current_audio(); });
        }
    }

    void load_path_request() {
        if (m_request.document) {
            m_document = *m_request.document;
        } else {
            std::string reason;
            m_document = load_document_summary(m_request.source_path, reason, m_request.keys);
            if (!m_document) {
                append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Loader rejected source: %1").arg(utf8_to_qstring(reason)));
            }
        }
        if (m_request.source_bytes) {
            m_bytes = std::move(*m_request.source_bytes);
            const auto* source_path = m_request.source_path.empty() ? nullptr : &m_request.source_path;
            try_load_transform(source_path);
            try_load_utf();
            try_load_archive();
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Opened independent byte-backed session: %1").arg(m_title));
            return;
        }

        if (auto bytes = read_file_bytes(m_request.source_path)) {
            m_bytes = std::move(*bytes);
            try_load_transform(&m_request.source_path);
            try_load_utf();
            try_load_archive();
        } else {
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Byte load failed: %1").arg(bytes.error()));
        }
        append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Opened independent path session: %1").arg(path_to_qstring(m_request.source_path)));
    }

    void load_archive_entry_request() {
        if (!m_request.entry) {
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Archive entry request was missing entry metadata."));
            return;
        }

        if (m_request.source_bytes) {
            m_bytes = std::move(*m_request.source_bytes);
            try_load_transform();
            try_load_utf();
            try_load_archive();
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Opened independent archive-entry byte session: %1 bytes").arg(static_cast<qulonglong>(m_bytes.size())));
        } else {
            if (auto bytes = load_embedded_entry_bytes(*m_request.entry, m_request.keys)) {
                m_bytes = std::move(*bytes);
                try_load_transform();
                try_load_utf();
                try_load_archive();
                append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Materialized archive entry copy: %1 bytes").arg(static_cast<qulonglong>(m_bytes.size())));
            } else {
                append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Entry materialization failed: %1").arg(utf8_to_qstring(bytes.error())));
            }
        }

        std::string reason;
        if (m_usm) {
            auto document = modules::usm::summarize(
                path_from_utf8(m_request.entry->name), *m_usm, {}, false);
            finalize_embedded_document_summary(document, *m_request.entry, m_bytes.size());
            attach_nested_sources(document, *m_request.entry);
            m_document = std::move(document);
        } else if (m_sfd) {
            auto document = modules::sfd::summarize(path_from_utf8(m_request.entry->name), *m_sfd);
            finalize_embedded_document_summary(document, *m_request.entry, m_bytes.size());
            attach_nested_sources(document, *m_request.entry);
            m_document = std::move(document);
        } else {
            m_document = load_embedded_entry_summary(*m_request.entry, reason, m_request.keys);
        }
        if (!m_document && !reason.empty()) {
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Embedded summary unavailable: %1").arg(utf8_to_qstring(reason)));
        }
    }

    void load_scratch_request() {
        auto scratch = create_scratch_editor_session(m_request);
        m_document = std::move(scratch.document);
        m_bytes = std::move(scratch.bytes);
        m_utf = std::move(scratch.utf);
        m_afs = std::move(scratch.afs);
        m_awb = std::move(scratch.awb);
        m_acx = std::move(scratch.acx);
        m_cpk = std::move(scratch.cpk);
        m_cvm = std::move(scratch.cvm);
        m_archive_kind = scratch.archive_kind;
        m_transform_kind = scratch.transform_kind;
        if (scratch.title) {
            m_title = editor_label(*scratch.title);
            m_ui.title_label->setText(m_title);
        }
        for (const auto& message : scratch.log_messages) {
            append_log(message);
        }
    }

    void try_load_utf() {
        if (m_transform_kind != TransformKind::None && m_transform_kind != TransformKind::Acb) {
            return;
        }
        const auto format = editor_format_id(m_request).toLower();
        const auto has_utf_magic = m_bytes.size() >= 4 &&
            m_bytes[0] == static_cast<uint8_t>('@') &&
            m_bytes[1] == static_cast<uint8_t>('U') &&
            m_bytes[2] == static_cast<uint8_t>('T') &&
            m_bytes[3] == static_cast<uint8_t>('F');
        if (!format.contains(QStringLiteral("utf")) && !has_utf_magic) {
            return;
        }
        auto loaded = cricodecs::utf::UtfTable::load(std::span<const uint8_t>(m_bytes.data(), m_bytes.size()));
        if (loaded) {
            m_utf = loaded->editable_copy();
            append_log(m_transform_kind == TransformKind::Acb
                ? QCoreApplication::translate("Editor.EditorDocumentWidget", "ACB root UTF loaded for native-backed table editing. Embedded subtable blobs remain editable as VLData.")
                : QCoreApplication::translate("Editor.EditorDocumentWidget", "UTF object loaded for native-backed editor mutations."));
        }
    }

    void try_load_transform(const std::filesystem::path* source_path = nullptr) {
        if (m_bytes.empty()) {
            return;
        }
        const auto span = std::span<const uint8_t>(m_bytes.data(), m_bytes.size());
        auto loaded = try_load_transform_editor_session(span, m_request, source_path);
        m_adx = std::move(loaded.adx);
        m_hca = std::move(loaded.hca);
        m_aax = std::move(loaded.aax);
        m_aix = std::move(loaded.aix);
        m_usm = std::move(loaded.usm);
        m_sfd = std::move(loaded.sfd);
        m_csb = std::move(loaded.csb);
        m_acb = std::move(loaded.acb);
        m_transform_kind = loaded.kind;
        for (const auto& message : loaded.log_messages) {
            append_log(message);
        }
    }

    void try_load_archive() {
        if (m_utf || m_transform_kind != TransformKind::None || m_bytes.empty()) {
            return;
        }
        const auto span = std::span<const uint8_t>(m_bytes.data(), m_bytes.size());
        auto loaded = try_load_archive_editor_session(span, m_request);
        m_afs = std::move(loaded.afs);
        m_awb = std::move(loaded.awb);
        m_acx = std::move(loaded.acx);
        m_cpk = std::move(loaded.cpk);
        m_cvm = std::move(loaded.cvm);
        m_archive_kind = loaded.kind;
        for (const auto& message : loaded.log_messages) {
            append_log(message);
        }
    }

    void refresh_summary() {
        update_local_key_panel_visibility();
        populate_info();
        if (m_utf) {
            refresh_utf_view();
            return;
        }
        if (m_transform_kind != TransformKind::None) {
            refresh_transform_view();
            return;
        }
        if (m_archive_kind != ArchiveKind::None) {
            refresh_archive_view();
            return;
        }
        m_ui.archive_toolbar->hide();
        m_ui.archive_table->hide();
        m_ui.transform_toolbar->hide();
        m_ui.transform_table->hide();
        m_ui.utf_toolbar->hide();
        m_ui.utf_grid->hide();
        m_ui.schema_table->hide();
        m_ui.utf_edit_panel->hide();
        m_ui.binary_actions_panel->hide();
        m_ui.table->show();
        if (m_document) {
            m_ui.table_model->set_entries(m_document->entries, m_document->entry_columns, m_document->entry_column_types);
            m_ui.table->setRootIsDecorated(!m_ui.table_model->has_custom_columns());
            if (!m_ui.table_model->has_custom_columns()) {
                m_ui.table->expandToDepth(6);
            }
            for (int column = 0; column < (std::min)(5, m_ui.table_model->columnCount()); ++column) {
                m_ui.table->resizeColumnToContents(column);
            }
        } else {
            m_ui.table_model->clear();
        }
        show_hex_preview(std::span<const uint8_t>(m_bytes.data(), m_bytes.size()));
    }

    void sync_local_key_mode_controls() {
        const bool adx_context = m_transform_kind == TransformKind::Adx ||
            m_transform_kind == TransformKind::Aax || m_transform_kind == TransformKind::Aix;
        const bool cvm_context = m_archive_kind == ArchiveKind::Cvm;
        const auto adx_mode = static_cast<DecryptionKeys::AdxMode>(m_ui.local_key_type->currentData().toInt());
        m_ui.local_key_type->setVisible(adx_context);
        m_ui.adx_triplet_panel->setVisible(adx_context && adx_mode == DecryptionKeys::AdxMode::AhxTriplet);
        m_ui.adx_subkey_panel->setVisible(adx_context && adx_mode == DecryptionKeys::AdxMode::Type9Number);
        m_ui.cri_key_edit->setVisible(!adx_context || (adx_mode != DecryptionKeys::AdxMode::None &&
            adx_mode != DecryptionKeys::AdxMode::AhxTriplet));
        m_ui.cri_key_base->setVisible(!cvm_context && (!adx_context || adx_mode == DecryptionKeys::AdxMode::Type9Number));
        if (adx_context) {
            m_ui.cri_key_edit->setPlaceholderText(adx_mode == DecryptionKeys::AdxMode::Type8String
                ? QCoreApplication::translate("Editor.EditorDocumentWidget", "Type 8 key string")
                : QCoreApplication::translate("Editor.EditorDocumentWidget", "Type 9 key"));
        }
    }

    void sync_local_cri_key_ui() {
        const bool adx_context = m_transform_kind == TransformKind::Adx ||
            m_transform_kind == TransformKind::Aax || m_transform_kind == TransformKind::Aix;
        const bool cvm_context = m_archive_kind == ArchiveKind::Cvm;
        m_ui.local_key_label->setText(cvm_context
            ? QCoreApplication::translate("Editor.EditorDocumentWidget", "CVM scramble key")
            : adx_context ? QCoreApplication::translate("Editor.EditorDocumentWidget", "Local ADX/AHX key") : QCoreApplication::translate("Editor.EditorDocumentWidget", "Local CRI key"));
        {
            const QSignalBlocker blocker(m_ui.cvm_scramble_check);
            m_ui.cvm_scramble_panel->setVisible(cvm_context);
            m_ui.cvm_scramble_check->setChecked(cvm_context && !m_cvm_scramble_key.empty());
        }
        if (cvm_context) {
            m_ui.cri_key_edit->setPlaceholderText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Scramble key string"));
            m_ui.cri_key_edit->setToolTip(QCoreApplication::translate("Editor.EditorDocumentWidget", "This key is used only when this editor tab rebuilds the CVM. An empty key writes an unscrambled CVM."));
            m_ui.cri_key_edit->setText(utf8_to_qstring(m_cvm_scramble_key));
            m_ui.cri_key_edit->setEnabled(m_ui.cvm_scramble_check->isChecked());
            sync_local_key_mode_controls();
            return;
        }
        m_ui.cri_key_edit->setEnabled(true);
        if (adx_context) {
            {
                const QSignalBlocker blocker(m_ui.local_key_type);
                if (const auto index = m_ui.local_key_type->findData(static_cast<int>(m_request.keys.adx_mode)); index >= 0) {
                    m_ui.local_key_type->setCurrentIndex(index);
                }
            }
            m_ui.cri_key_edit->setToolTip(QCoreApplication::translate("Editor.EditorDocumentWidget", "This ADX/AHX key belongs only to this editor tab."));
            m_ui.adx_subkey_spin->setValue(m_request.keys.adx_subkey);
            switch (m_request.keys.adx_mode) {
            case DecryptionKeys::AdxMode::Type8String:
                m_ui.cri_key_edit->setText(utf8_to_qstring(m_request.keys.adx_type8_key));
                break;
            case DecryptionKeys::AdxMode::Type9Number:
                m_ui.cri_key_edit->setText(m_ui.cri_key_base->currentData().toInt() == 10
                    ? QString::number(m_request.keys.adx_type9_key)
                    : QStringLiteral("%1").arg(m_request.keys.adx_type9_key, 16, 16, QLatin1Char('0')).toUpper());
                break;
            case DecryptionKeys::AdxMode::AhxTriplet:
                m_ui.adx_triplet_start->setText(QStringLiteral("%1").arg(m_request.keys.ahx_start, 4, 16, QLatin1Char('0')).toUpper());
                m_ui.adx_triplet_mult->setText(QStringLiteral("%1").arg(m_request.keys.ahx_mult, 4, 16, QLatin1Char('0')).toUpper());
                m_ui.adx_triplet_add->setText(QStringLiteral("%1").arg(m_request.keys.ahx_add, 4, 16, QLatin1Char('0')).toUpper());
                break;
            case DecryptionKeys::AdxMode::None:
                m_ui.cri_key_edit->clear();
                break;
            }
            sync_local_key_mode_controls();
            return;
        }
        m_ui.cri_key_edit->setPlaceholderText(QCoreApplication::translate("Editor.EditorDocumentWidget", "No key"));
        m_ui.cri_key_edit->setToolTip(QCoreApplication::translate("Editor.EditorDocumentWidget", "This key belongs only to this editor tab and does not change the global CRI key."));
        if (!m_request.keys.has_cri_key) {
            m_ui.cri_key_edit->clear();
            sync_local_key_mode_controls();
            return;
        }
        const auto base = m_ui.cri_key_base->currentData().toInt();
        m_ui.cri_key_edit->setText(base == 10
            ? QString::number(m_request.keys.cri_key)
            : QStringLiteral("%1").arg(m_request.keys.cri_key, 16, 16, QLatin1Char('0')).toUpper());
        sync_local_key_mode_controls();
    }

    void update_local_key_panel_visibility() {
        const bool relevant = m_transform_kind == TransformKind::Hca ||
            m_transform_kind == TransformKind::Adx ||
            m_transform_kind == TransformKind::Aax ||
            m_transform_kind == TransformKind::Aix ||
            m_transform_kind == TransformKind::Usm ||
            m_transform_kind == TransformKind::Sfd ||
            m_transform_kind == TransformKind::Acb ||
            m_archive_kind == ArchiveKind::Awb ||
            m_archive_kind == ArchiveKind::Cvm;
        m_ui.cri_key_panel->setVisible(relevant);
    }

    void apply_local_cri_key() {
        auto text = m_ui.cri_key_edit->text().trimmed();
        if (m_archive_kind == ArchiveKind::Cvm) {
            if (m_ui.cvm_scramble_check->isChecked()) {
                if (text.isEmpty()) {
                    QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Missing scramble key"), QCoreApplication::translate("Editor.EditorDocumentWidget", "Enter a CVM scramble key string or disable Scramble on save."));
                    return;
                }
                m_cvm_scramble_key = qstring_to_utf8(text);
                append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Enabled scrambled CVM output for this editor tab."));
            } else {
                m_cvm_scramble_key.clear();
                append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "CVM output will be written unscrambled."));
            }
            m_dirty = true;
            refresh_title();
            sync_local_cri_key_ui();
            return;
        }

        const bool adx_context = m_transform_kind == TransformKind::Adx ||
            m_transform_kind == TransformKind::Aax || m_transform_kind == TransformKind::Aix;
        if (adx_context) {
            const auto mode = static_cast<DecryptionKeys::AdxMode>(m_ui.local_key_type->currentData().toInt());
            if (mode == DecryptionKeys::AdxMode::None) {
                m_request.keys.adx_mode = DecryptionKeys::AdxMode::None;
                m_request.keys.adx_type8_key.clear();
                m_request.keys.adx_type9_key = 0;
                m_request.keys.adx_subkey = 0;
                m_request.keys.ahx_start = 0;
                m_request.keys.ahx_mult = 0;
                m_request.keys.ahx_add = 0;
            } else if (mode == DecryptionKeys::AdxMode::AhxTriplet) {
                const std::array parts{
                    m_ui.adx_triplet_start->text().trimmed(),
                    m_ui.adx_triplet_mult->text().trimmed(),
                    m_ui.adx_triplet_add->text().trimmed()
                };
                std::array<uint16_t, 3> values{};
                bool valid = true;
                for (int index = 0; valid && index < 3; ++index) {
                    bool ok = false;
                    values[static_cast<size_t>(index)] = parts[static_cast<size_t>(index)].toUShort(&ok, 16);
                    valid = ok && !parts[static_cast<size_t>(index)].isEmpty();
                }
                if (!valid) {
                    QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Invalid ADX/AHX key"),
                        QCoreApplication::translate("Editor.EditorDocumentWidget", "Start, Mult, and Add must each be a hexadecimal 16-bit value."));
                    return;
                }
                m_request.keys.adx_mode = DecryptionKeys::AdxMode::AhxTriplet;
                m_request.keys.ahx_start = values[0];
                m_request.keys.ahx_mult = values[1];
                m_request.keys.ahx_add = values[2];
            } else if (mode == DecryptionKeys::AdxMode::Type8String) {
                if (text.isEmpty()) {
                    QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Invalid ADX key"), QCoreApplication::translate("Editor.EditorDocumentWidget", "The type-8 key string is empty."));
                    return;
                }
                m_request.keys.adx_mode = DecryptionKeys::AdxMode::Type8String;
                m_request.keys.adx_type8_key = qstring_to_utf8(text);
            } else if (mode == DecryptionKeys::AdxMode::Type9Number) {
                auto numeric = text;
                numeric.remove(QLatin1Char('_'));
                if (numeric.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
                    numeric.remove(0, 2);
                }
                bool ok = false;
                const auto value = numeric.toULongLong(&ok, m_ui.cri_key_base->currentData().toInt());
                if (!ok || numeric.isEmpty()) {
                    QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Invalid ADX key"),
                        QCoreApplication::translate("Editor.EditorDocumentWidget", "Enter a valid %1 64-bit type-9 key.")
                            .arg(m_ui.cri_key_base->currentData().toInt() == 16
                                ? QCoreApplication::translate("Editor.EditorDocumentWidget", "hexadecimal") : QCoreApplication::translate("Editor.EditorDocumentWidget", "decimal")));
                    return;
                }
                m_request.keys.adx_mode = DecryptionKeys::AdxMode::Type9Number;
                m_request.keys.adx_type9_key = value;
                m_request.keys.adx_subkey = static_cast<uint16_t>(m_ui.adx_subkey_spin->value());
            }
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Applied an ADX/AHX key locally to this editor tab."));
            sync_local_cri_key_ui();
            dismiss_editor_media_preview();
            if ((m_transform_kind == TransformKind::Aax || m_transform_kind == TransformKind::Aix) &&
                m_ui.transform_table->currentIndex().isValid()) {
                show_selected_transform_row(m_ui.transform_table->currentIndex().row());
            } else {
                preview_current_audio();
            }
            return;
        }

        text.remove(QLatin1Char('_'));
        const auto base = m_ui.cri_key_base->currentData().toInt();
        if (base == 16) {
            text.remove(QLatin1Char(' '));
            if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
                text.remove(0, 2);
            }
        }
        if (text.isEmpty()) {
            m_request.keys.has_cri_key = false;
            m_request.keys.cri_key = 0;
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Cleared the local CRI key for this editor tab."));
        } else {
            bool ok = false;
            const auto value = text.toULongLong(&ok, base);
            if (!ok) {
                QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Invalid CRI key"),
                    QCoreApplication::translate("Editor.EditorDocumentWidget", "Enter a valid %1 64-bit key.").arg(base == 16 ? QCoreApplication::translate("Editor.EditorDocumentWidget", "hexadecimal") : QCoreApplication::translate("Editor.EditorDocumentWidget", "decimal")));
                return;
            }
            m_request.keys.has_cri_key = true;
            m_request.keys.cri_key = value;
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Applied a CRI key locally to this editor tab. The global key was not changed."));
        }
        sync_local_cri_key_ui();
        dismiss_editor_media_preview();
        if (m_transform_kind == TransformKind::Usm) {
            try_load_transform();
            refresh_transform_view();
        }
        if (m_transform_kind == TransformKind::Usm || m_transform_kind == TransformKind::Sfd) {
            preview_current_mux();
        } else if (m_transform_kind == TransformKind::Hca) {
            preview_current_audio();
        } else if (m_archive_kind != ArchiveKind::None) {
            refresh_archive_view();
        } else if (m_utf) {
            refresh_utf_view();
        }
    }

    [[nodiscard]] bool utf_transposed() const noexcept {
        return m_utf && m_utf->row_count() == 1;
    }

    std::pair<int, int> utf_source_cell(int view_row, int view_column) const {
        if (!utf_transposed()) {
            return {view_row, view_column};
        }
        return view_row < 0 ? std::pair{-1, -1} : std::pair{0, view_row};
    }

    void refresh_utf_view() {
        m_refreshing_utf = true;
        m_ui.table->hide();
        m_ui.field_table->hide();
        m_ui.archive_toolbar->hide();
        m_ui.archive_table->hide();
        const bool editing_acb = m_transform_kind == TransformKind::Acb;
        m_ui.transform_toolbar->setVisible(editing_acb);
        m_ui.transform_table->hide();
        m_ui.utf_toolbar->show();
        m_ui.utf_grid->show();
        m_ui.schema_table->setVisible(!utf_transposed());
        m_ui.utf_edit_panel->show();
        m_ui.binary_actions_panel->show();
        m_ui.apply_value_button->setEnabled(false);
        m_ui.rename_column_button->setEnabled(false);
        m_ui.replace_binary_button->setEnabled(false);

        if (editing_acb) {
            m_ui.transform_kind_label->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "ACB root UTF"));
            for (auto* button : {
                    m_ui.encode_transform_button, m_ui.decode_transform_button, m_ui.decrypt_transform_button,
                    m_ui.encrypt_transform_button, m_ui.rebuild_transform_button, m_ui.transform_options_button,
                    m_ui.adx_container_build_button, m_ui.csb_directory_build_button,
                    m_ui.media_build_wizard_button, m_ui.editor_mux_preview_button}) {
                button->hide();
            }
            m_ui.extract_transform_button->show();
            m_ui.open_acb_awb_button->show();
            m_ui.export_acb_awb_button->show();
            m_ui.extract_transform_button->setEnabled(!m_acb_semantic_state_stale);
            m_ui.open_acb_awb_button->setEnabled(!m_acb_semantic_state_stale && m_acb.has_value());
            m_ui.export_acb_awb_button->setEnabled(!m_acb_semantic_state_stale && m_acb.has_value());
            const auto saved_state_note = QCoreApplication::translate("Editor.EditorDocumentWidget", "Save and reopen after editing before using ACB extraction or associated-AWB actions.");
            m_ui.extract_transform_button->setToolTip(m_acb_semantic_state_stale ? saved_state_note : QString{});
            m_ui.open_acb_awb_button->setToolTip(m_acb_semantic_state_stale ? saved_state_note : QString{});
            m_ui.export_acb_awb_button->setToolTip(m_acb_semantic_state_stale ? saved_state_note : QString{});
            m_ui.transform_filter_edit->hide();
        }

        modules::utf::populate_utf_tables(*m_utf, {
            .table_name = m_ui.table_name_edit,
            .grid = m_ui.utf_grid,
            .schema = m_ui.schema_table,
            .transpose_single_row = true,
        });
        m_bytes = modules::utf::build_session_bytes(*m_utf);
        show_hex_preview(std::span<const uint8_t>(m_bytes.data(), m_bytes.size()));
        m_refreshing_utf = false;
        if (m_ui.utf_grid->currentRow() < 0 && m_ui.utf_grid->rowCount() > 0 && m_ui.utf_grid->columnCount() > 0) {
            m_ui.utf_grid->setCurrentCell(0, utf_transposed() ? 2 : 0);
        }
        const auto [source_row, source_column] = utf_source_cell(
            m_ui.utf_grid->currentRow(), m_ui.utf_grid->currentColumn());
        show_selected_utf_cell(source_row, source_column);
    }

    void refresh_archive_view() {
        m_refreshing_archive = true;
        refresh_archive_document_ui(m_ui, archive_view(), m_request.keys);
        m_refreshing_archive = false;
        show_selected_archive_entry(m_ui.archive_table->currentRow());
    }

    TransformSessionView transform_view() {
        return {
            .adx = m_adx ? &*m_adx : nullptr,
            .hca = m_hca ? &*m_hca : nullptr,
            .aax = m_aax ? &*m_aax : nullptr,
            .aix = m_aix ? &*m_aix : nullptr,
            .usm = m_usm ? &*m_usm : nullptr,
            .sfd = m_sfd ? &*m_sfd : nullptr,
            .csb = m_csb ? &*m_csb : nullptr,
            .acb = m_acb ? &*m_acb : nullptr,
            .keys = &m_request.keys,
        };
    }

    void refresh_transform_view() {
        m_transform_rows = transform_detail_rows(m_transform_kind, transform_view());
        m_usm_chunk_rows = m_transform_kind == TransformKind::Usm && m_usm
            ? modules::usm::chunk_detail_rows(*m_usm)
            : std::vector<modules::TransformDetailRow>{};
        refresh_transform_rows_ui();
    }

    void refresh_transform_rows_ui() {
        refresh_visible_transform_rows();
        refresh_transform_document_ui(
            m_ui,
            m_transform_kind,
            transform_view(),
            m_visible_transform_rows,
            QString{}
        );
        if (m_ui.transform_table->currentIndex().isValid()) {
            show_selected_transform_row(m_ui.transform_table->currentIndex().row());
        } else {
            show_hex_preview(std::span<const uint8_t>(m_bytes.data(), m_bytes.size()));
        }
    }

    static bool transform_row_matches(const modules::TransformDetailRow& detail, QStringView filter_text) {
        if (filter_text.isEmpty()) {
            return true;
        }
        return detail.field.contains(filter_text, Qt::CaseInsensitive) ||
            detail.value.contains(filter_text, Qt::CaseInsensitive);
    }

    void refresh_visible_transform_rows() {
        const auto filter_text = m_ui.transform_filter_edit->text().trimmed();
        m_visible_transform_rows.clear();
        m_visible_transform_rows.reserve(m_transform_rows.size() + m_usm_chunk_rows.size());
        for (const auto& row : m_transform_rows) {
            if (transform_row_matches(row, filter_text)) {
                m_visible_transform_rows.push_back(row);
            }
        }
        for (const auto& row : m_usm_chunk_rows) {
            if (transform_row_matches(row, filter_text)) {
                m_visible_transform_rows.push_back(row);
            }
        }
    }

    void populate_info() {
        auto archive = archive_view();
        auto transform = transform_view();
        refresh_document_info_ui(m_ui, this, {
            .request = &m_request,
            .byte_count = m_bytes.size(),
            .utf = m_utf ? &*m_utf : nullptr,
            .archive = &archive,
            .transform_kind = m_transform_kind,
            .transform = &transform
        });
    }

    void show_selected_entry(const QModelIndex& index) {
        if (!index.isValid()) {
            dismiss_editor_media_preview();
            clear_raw_preview();
            set_preview_tabs(false, false, 0);
            return;
        }
        const auto* summary = m_ui.table_model->summary_at(index);
        if (summary == nullptr) {
            return;
        }
        if (!summary->inspector_entries.empty()) {
            m_ui.field_model->set_entries(summary->inspector_entries, {cristudio::i18n::translate_utf8("Editor.EditorDocumentWidget", "Field"), cristudio::i18n::translate_utf8("Editor.EditorDocumentWidget", "Type"), cristudio::i18n::translate_utf8("Editor.EditorDocumentWidget", "Value")}, {"field", "type", "value"});
            m_ui.field_table->show();
            m_ui.field_table->resizeColumnToContents(0);
            m_ui.field_table->resizeColumnToContents(1);
        } else {
            m_ui.field_model->clear();
            m_ui.field_table->hide();
        }
        preview_entry_bytes(*summary);
    }

    void show_selected_field(const QModelIndex& index) {
        if (!index.isValid()) {
            dismiss_editor_media_preview();
            clear_raw_preview();
            set_preview_tabs(false, false, 0);
            return;
        }
        const auto* summary = m_ui.field_model->summary_at(index);
        if (summary == nullptr) {
            return;
        }
        preview_entry_bytes(*summary);
    }

    void show_selected_utf_cell(int row, int column) {
        if (!m_utf || row < 0 || column < 0 ||
            row >= static_cast<int>(m_utf->row_count()) ||
            column >= static_cast<int>(m_utf->column_count())) {
            m_ui.apply_value_button->setEnabled(false);
            m_ui.rename_column_button->setEnabled(false);
            m_ui.replace_binary_button->setEnabled(false);
            m_ui.value_edit->clear();
            m_ui.value_edit->setValidator(nullptr);
            m_ui.value_edit->setReadOnly(true);
            m_ui.value_type_label->clear();
            dismiss_editor_media_preview();
            clear_raw_preview();
            set_preview_tabs(false, false, 0);
            return;
        }

        const auto value = modules::utf::utf_value_for_cell(*m_utf, static_cast<uint32_t>(row), static_cast<uint32_t>(column));
        m_ui.value_edit->setText(utf_value_text(value));
        const auto type = m_utf->column(static_cast<uint32_t>(column)).type;
        m_ui.value_type_label->setText(modules::utf::utf_type_name(type));
        m_ui.value_edit->setReadOnly(type == cricodecs::utf::ColumnType::VLData);
        m_ui.value_edit->setMaxLength(32767);
        m_ui.value_edit->setPlaceholderText({});
        using cricodecs::utf::ColumnType;
        switch (type) {
        case ColumnType::UInt8:
        case ColumnType::UInt16:
        case ColumnType::UInt32:
        case ColumnType::UInt64:
            m_ui.value_edit->setValidator(m_ui.unsigned_value_validator);
            m_ui.value_edit->setPlaceholderText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Unsigned integer"));
            break;
        case ColumnType::SInt8:
        case ColumnType::SInt16:
        case ColumnType::SInt32:
        case ColumnType::SInt64:
            m_ui.value_edit->setValidator(m_ui.signed_value_validator);
            m_ui.value_edit->setPlaceholderText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Signed integer"));
            break;
        case ColumnType::Float:
        case ColumnType::Double:
            m_ui.value_edit->setValidator(m_ui.real_value_validator);
            m_ui.value_edit->setPlaceholderText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Floating-point value"));
            break;
        case ColumnType::GUID:
            m_ui.value_edit->setValidator(m_ui.guid_value_validator);
            m_ui.value_edit->setMaxLength(32);
            m_ui.value_edit->setPlaceholderText(QCoreApplication::translate("Editor.EditorDocumentWidget", "32 hexadecimal digits"));
            break;
        case ColumnType::String:
            m_ui.value_edit->setValidator(nullptr);
            m_ui.value_edit->setPlaceholderText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Text"));
            break;
        case ColumnType::VLData:
            m_ui.value_edit->setValidator(nullptr);
            m_ui.value_edit->setPlaceholderText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Use Replace Binary From File"));
            break;
        }
        m_ui.apply_value_button->setEnabled(type != cricodecs::utf::ColumnType::VLData);
        m_ui.rename_column_button->setEnabled(true);
        m_ui.replace_binary_button->setEnabled(type == cricodecs::utf::ColumnType::VLData);
        if (type == cricodecs::utf::ColumnType::VLData) {
            if (auto data = m_utf->get_data(static_cast<uint32_t>(row), static_cast<uint32_t>(column))) {
                if (data->size() >= 4 && (*data)[0] == static_cast<uint8_t>('@') &&
                    (*data)[1] == static_cast<uint8_t>('U') && (*data)[2] == static_cast<uint8_t>('T') &&
                    (*data)[3] == static_cast<uint8_t>('F')) {
                    if (auto nested = cricodecs::utf::UtfTable::load(*data)) {
                        dismiss_editor_media_preview();
                        set_raw_preview(*data, data->size(), "UTF");
                        modules::utf::populate_utf_tables(*nested, {
                            .grid = m_ui.payload_table,
                            .transpose_single_row = true,
                        });
                        m_ui.hex_preview->hide();
                        m_ui.payload_table->show();
                        set_preview_tabs(true, raw_preview_available(), 0);
                        return;
                    }
                }
                show_hex_preview(*data, 0, "UTF");
            } else {
                show_detail_preview(QCoreApplication::translate("Editor.EditorDocumentWidget", "Error: %1").arg(utf8_to_qstring(data.error())));
            }
        } else {
            m_ui.payload_table->hide();
            clear_raw_preview();
            set_preview_tabs(false, false, 0);
        }
    }

    ArchiveSessionView archive_view() const {
        return ArchiveSessionView{
            .kind = m_archive_kind,
            .afs = m_afs ? &*m_afs : nullptr,
            .awb = m_awb ? &*m_awb : nullptr,
            .acx = m_acx ? &*m_acx : nullptr,
            .cpk = m_cpk ? &*m_cpk : nullptr,
            .cvm = m_cvm ? &*m_cvm : nullptr
        };
    }

    MutableArchiveSessionView mutable_archive_view() {
        return MutableArchiveSessionView{
            .kind = m_archive_kind,
            .afs = m_afs ? &*m_afs : nullptr,
            .awb = m_awb ? &*m_awb : nullptr,
            .acx = m_acx ? &*m_acx : nullptr,
            .cpk = m_cpk ? &*m_cpk : nullptr,
            .cvm = m_cvm ? &*m_cvm : nullptr,
            .cpk_obfuscate_utf = &m_cpk_obfuscate_utf
        };
    }

    int selected_archive_index() const {
        return validated_archive_index(archive_view(), m_ui.archive_table->currentRow());
    }

    void show_selected_archive_entry(int row) {
        if (row < 0 || m_archive_kind == ArchiveKind::None) {
            dismiss_editor_media_preview();
            clear_raw_preview();
            set_preview_tabs(false, false, 0);
            return;
        }
        const auto view = archive_view();
        auto bytes = archive_entry_bytes(view, static_cast<uint32_t>(row), m_preview_scratch);
        if (!bytes) {
            show_detail_preview(QCoreApplication::translate("Editor.EditorDocumentWidget", "Error: %1").arg(bytes.error()));
            return;
        }
        if (bytes->size() >= 4 && (*bytes)[0] == static_cast<uint8_t>('@') &&
            (*bytes)[1] == static_cast<uint8_t>('U') && (*bytes)[2] == static_cast<uint8_t>('T') &&
            (*bytes)[3] == static_cast<uint8_t>('F')) {
            if (auto table = cricodecs::utf::UtfTable::load(*bytes)) {
                dismiss_editor_media_preview();
                set_raw_preview(*bytes, bytes->size(), "UTF");
                modules::utf::populate_utf_tables(*table, {
                    .grid = m_ui.payload_table,
                    .transpose_single_row = true,
                });
                m_ui.hex_preview->hide();
                m_ui.payload_table->show();
                set_preview_tabs(true, raw_preview_available(), 0);
                return;
            }
        }
        if (auto format = direct_audio_format(*bytes)) {
            LoadedDocument document;
            if (const auto* name_item = m_ui.archive_table->item(row, 0); name_item != nullptr) {
                document.display_name = qstring_to_utf8(name_item->text());
            }
            document.format = std::move(*format);
            start_audio_preview(std::move(document), std::vector<uint8_t>(bytes->begin(), bytes->end()));
            return;
        }
        show_hex_preview(*bytes);
    }

    void show_selected_transform_row(int row) {
        if (row < 0 || m_transform_kind == TransformKind::None) {
            dismiss_editor_media_preview();
            clear_raw_preview();
            set_preview_tabs(false, false, 0);
            return;
        }
        m_ui.payload_table->hide();
        m_ui.hex_preview->hide();
        const auto* detail = m_ui.transform_model->detail_at(row);
        if (detail == nullptr) {
            return;
        }
        const TransformPayloadSelection selection {
            .payload_kind = detail->payload_kind,
            .index = detail->index,
            .layer = detail->layer,
        };
        if (m_transform_kind == TransformKind::Usm && selection.index >= 0 &&
            (selection.payload_kind == 3 || selection.payload_kind == 10)) {
            dismiss_editor_media_preview();
            clear_raw_preview();
            set_preview_tabs(false, false, 0);
            return;
        }
        std::optional<cricodecs::utf::UtfTable> table = std::nullopt;
        if (m_transform_kind == TransformKind::Acb && m_acb) {
            if (auto loaded = modules::acb::payload_table(*m_acb, selection.payload_kind, selection.index)) {
                table = std::move(*loaded);
            }
        } else if (m_transform_kind == TransformKind::Csb && m_csb) {
            if (auto loaded = modules::csb::payload_table(*m_csb, selection.payload_kind, selection.index)) {
                table = std::move(*loaded);
            }
        }
        if (table) {
            dismiss_editor_media_preview();
            clear_raw_preview();
            modules::utf::populate_utf_tables(*table, {
                .grid = m_ui.payload_table,
                .transpose_single_row = true,
            });
            m_ui.hex_preview->hide();
            m_ui.payload_table->show();
            set_preview_tabs(true, raw_preview_available(), 0);
            return;
        }
        if (m_transform_kind == TransformKind::Usm && m_usm) {
            if (selection.payload_kind == 13 && selection.index >= 0) {
                preview_usm_stream(selection.index, *detail);
                return;
            }
        }
        auto bytes = transform_payload_preview_bytes(transform_view(), selection);
        if (bytes) {
            if (auto format = direct_audio_format(*bytes)) {
                LoadedDocument document;
                document.display_name = qstring_to_utf8(detail->field);
                document.format = std::move(*format);
                start_audio_preview(std::move(document), std::move(*bytes));
                return;
            }
            show_hex_preview(*bytes);
            return;
        }
        if (selection.payload_kind == 0 && !m_bytes.empty()) {
            show_hex_preview(std::span<const uint8_t>(m_bytes.data(), m_bytes.size()));
            return;
        }
        show_detail_preview(transform_payload_preview_text(
            transform_view(),
            selection,
            std::span<const uint8_t>(m_bytes.data(), m_bytes.size())
        ));
    }

    std::optional<TransformPayloadSelection> selected_transform_edit_target() const {
        if (!m_ui.transform_table->currentIndex().isValid()) {
            return std::nullopt;
        }
        const auto* detail = m_ui.transform_model->detail_at(m_ui.transform_table->currentIndex().row());
        if (detail == nullptr) {
            return std::nullopt;
        }
        return TransformPayloadSelection{
            .payload_kind = detail->payload_kind,
            .index = detail->index,
            .layer = detail->layer,
        };
    }

    std::optional<int> selected_editable_transform_index() const {
        const auto selection = selected_transform_edit_target();
        if (!selection) {
            return std::nullopt;
        }
        if ((m_transform_kind == TransformKind::Aax && selection->payload_kind == 1) ||
            (m_transform_kind == TransformKind::Csb && selection->payload_kind == 5)) {
            return selection->index;
        }
        return std::nullopt;
    }

    void finish_transform_entry_edit(
        QString action,
        int preferred_index,
        int preferred_kind = -1,
        int preferred_layer = -1
    ) {
        std::expected<std::vector<uint8_t>, std::string> bytes =
            std::unexpected(cristudio::i18n::translate_utf8("Editor.EditorDocumentWidget", "No editable transform container is loaded"));
        if (m_transform_kind == TransformKind::Aax && m_aax) {
            bytes = m_aax->save();
        } else if (m_transform_kind == TransformKind::Aix && m_aix) {
            bytes = m_aix->save();
        } else if (m_transform_kind == TransformKind::Csb && m_csb) {
            bytes = m_csb->save();
        }
        if (!bytes) {
            QMessageBox::warning(this, action, utf8_to_qstring(bytes.error()));
            return;
        }
        m_bytes = std::move(*bytes);
        m_dirty = true;
        refresh_title();
        populate_info();
        refresh_transform_view();
        const auto wanted_kind = preferred_kind >= 0
            ? preferred_kind
            : m_transform_kind == TransformKind::Aax ? 1 : 5;
        const auto row = m_ui.transform_model->find_row(wanted_kind, preferred_index, preferred_layer);
        if (row >= 0) {
            m_ui.transform_table->setCurrentIndex(m_ui.transform_model->index(row, 0));
            m_ui.transform_table->selectRow(row);
        }
        append_log(action + QCoreApplication::translate("Editor.EditorDocumentWidget", " completed."));
    }

    void add_transform_entries() {
        if ((!m_aax && !m_aix && !m_csb) || save_running()) {
            return;
        }
        if (m_transform_kind == TransformKind::Aix && m_aix) {
            QMenu menu(this);
            auto* add_segment = menu.addAction(QCoreApplication::translate("Editor.EditorDocumentWidget", "Add Segment..."));
            add_segment->setToolTip(QCoreApplication::translate("Editor.EditorDocumentWidget", "Choose one ADX file per existing layer."));
            auto* add_layer = menu.addAction(QCoreApplication::translate("Editor.EditorDocumentWidget", "Add Layer..."));
            add_layer->setToolTip(QCoreApplication::translate("Editor.EditorDocumentWidget", "Choose one ADX file per existing segment."));
            const auto* chosen = menu.exec(m_ui.add_transform_entry_button->mapToGlobal(
                QPoint(0, m_ui.add_transform_entry_button->height())));
            if (chosen == nullptr) {
                return;
            }

            const bool adding_segment = chosen == add_segment;
            const auto expected_count = adding_segment ? m_aix->layers().size() : m_aix->segments().size();
            const auto files = QFileDialog::getOpenFileNames(
                this,
                adding_segment
                    ? QCoreApplication::translate("Editor.EditorDocumentWidget", "Choose one ADX file per AIX layer")
                    : QCoreApplication::translate("Editor.EditorDocumentWidget", "Choose one ADX file per AIX segment"),
                QString{},
                QCoreApplication::translate("Editor.EditorDocumentWidget", "ADX audio (*.adx *.sfa);;All files (*)"));
            if (files.isEmpty()) {
                return;
            }
            if (static_cast<size_t>(files.size()) != expected_count) {
                QMessageBox::warning(
                    this,
                    QCoreApplication::translate("Editor.EditorDocumentWidget", "Add failed"),
                    QCoreApplication::translate(
                        "Editor.EditorDocumentWidget",
                        "Choose exactly %n ADX file(s), in %1 order.",
                        nullptr,
                        static_cast<int>(expected_count))
                        .arg(adding_segment ? QCoreApplication::translate("Editor.EditorDocumentWidget", "layer") : QCoreApplication::translate("Editor.EditorDocumentWidget", "segment")));
                return;
            }

            std::vector<std::vector<uint8_t>> payloads;
            payloads.reserve(static_cast<size_t>(files.size()));
            for (const auto& file : files) {
                auto bytes = read_file_bytes(path_from_qstring(file));
                if (!bytes) {
                    QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Add failed"), bytes.error());
                    return;
                }
                payloads.push_back(std::move(*bytes));
            }

            std::expected<void, std::string> result = std::unexpected(cristudio::i18n::translate_utf8("Editor.EditorDocumentWidget", "AIX add failed"));
            int selection_kind = modules::aix::segment_row_kind;
            int selection_index = 0;
            if (adding_segment) {
                cricodecs::aix::AixBuildSegment segment{.layer_adx_data = std::move(payloads)};
                result = m_aix->add_segment(std::move(segment));
                selection_index = static_cast<int>(m_aix->segments().size()) - 1;
            } else {
                result = m_aix->add_layer(std::move(payloads));
                selection_kind = modules::aix::layer_row_kind;
                selection_index = static_cast<int>(m_aix->layers().size()) - 1;
            }
            if (!result) {
                QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Add failed"), utf8_to_qstring(result.error()));
                return;
            }
            finish_transform_entry_edit(
                adding_segment ? QCoreApplication::translate("Editor.EditorDocumentWidget", "Add AIX segment") : QCoreApplication::translate("Editor.EditorDocumentWidget", "Add AIX layer"),
                selection_index,
                selection_kind);
            return;
        }

        const auto files = QFileDialog::getOpenFileNames(
            this,
            m_transform_kind == TransformKind::Aax ? QCoreApplication::translate("Editor.EditorDocumentWidget", "Add AAX segments") : QCoreApplication::translate("Editor.EditorDocumentWidget", "Add CSB streams"),
            QString{},
            m_transform_kind == TransformKind::Aax
                ? QCoreApplication::translate("Editor.EditorDocumentWidget", "ADX/AHX audio (*.adx *.ahx *.sfa);;All files (*)")
                : QCoreApplication::translate("Editor.EditorDocumentWidget", "Supported audio (*.aax *.adx *.ahx *.hca);;All files (*)"));
        if (files.isEmpty()) {
            return;
        }
        int last_index = -1;
        for (const auto& file : files) {
            auto bytes = read_file_bytes(path_from_qstring(file));
            if (!bytes) {
                QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Add failed"), bytes.error());
                return;
            }
            auto result = m_transform_kind == TransformKind::Aax
                ? m_aax->add_segment(*bytes)
                : m_csb->add_file(*bytes, path_from_qstring(QFileInfo(file).fileName()));
            if (!result) {
                QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Add failed"), utf8_to_qstring(result.error()));
                return;
            }
            last_index = m_transform_kind == TransformKind::Aax
                ? static_cast<int>(m_aax->segment_count()) - 1
                : static_cast<int>(m_csb->stream_count()) - 1;
        }
        finish_transform_entry_edit(QCoreApplication::translate("Editor.EditorDocumentWidget", "Add entries"), last_index);
    }

    void replace_transform_entry() {
        if (m_transform_kind == TransformKind::Aix && m_aix) {
            const auto selection = selected_transform_edit_target();
            if (!selection || selection->payload_kind != modules::aix::payload_row_kind ||
                selection->index < 0 || selection->layer < 0) {
                QMessageBox::information(
                    this,
                    QCoreApplication::translate("Editor.EditorDocumentWidget", "Replace AIX audio"),
                    QCoreApplication::translate("Editor.EditorDocumentWidget", "Select a Layer N segment M ADX payload first."));
                return;
            }
            const auto file = QFileDialog::getOpenFileName(
                this,
                QCoreApplication::translate("Editor.EditorDocumentWidget", "Choose replacement ADX audio"),
                QString{},
                QCoreApplication::translate("Editor.EditorDocumentWidget", "ADX audio (*.adx *.sfa);;All files (*)"));
            if (file.isEmpty()) {
                return;
            }
            auto bytes = read_file_bytes(path_from_qstring(file));
            if (!bytes) {
                QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Replace failed"), bytes.error());
                return;
            }
            auto result = m_aix->replace_layer(
                static_cast<size_t>(selection->index),
                static_cast<size_t>(selection->layer),
                *bytes);
            if (!result) {
                QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Replace failed"), utf8_to_qstring(result.error()));
                return;
            }
            finish_transform_entry_edit(
                QCoreApplication::translate("Editor.EditorDocumentWidget", "Replace AIX audio"),
                selection->index,
                modules::aix::payload_row_kind,
                selection->layer);
            return;
        }

        const auto index = selected_editable_transform_index();
        if (!index) {
            QMessageBox::information(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Replace entry"), QCoreApplication::translate("Editor.EditorDocumentWidget", "Select an AAX segment or CSB stream first."));
            return;
        }
        const auto file = QFileDialog::getOpenFileName(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Choose replacement audio"));
        if (file.isEmpty()) {
            return;
        }
        auto bytes = read_file_bytes(path_from_qstring(file));
        if (!bytes) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Replace failed"), bytes.error());
            return;
        }
        auto result = m_transform_kind == TransformKind::Aax
            ? m_aax->replace_segment(static_cast<uint32_t>(*index), *bytes)
            : m_csb->replace_file(static_cast<uint32_t>(*index), *bytes);
        if (!result) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Replace failed"), utf8_to_qstring(result.error()));
            return;
        }
        finish_transform_entry_edit(QCoreApplication::translate("Editor.EditorDocumentWidget", "Replace entry"), *index);
    }

    void remove_transform_entry() {
        if (m_transform_kind == TransformKind::Aix && m_aix) {
            const auto selection = selected_transform_edit_target();
            if (!selection ||
                (selection->payload_kind != modules::aix::segment_row_kind &&
                 selection->payload_kind != modules::aix::layer_row_kind)) {
                QMessageBox::information(
                    this,
                    QCoreApplication::translate("Editor.EditorDocumentWidget", "Remove AIX entry"),
                    QCoreApplication::translate("Editor.EditorDocumentWidget", "Select a Segment or Layer summary row first."));
                return;
            }
            const bool removing_segment = selection->payload_kind == modules::aix::segment_row_kind;
            if (QMessageBox::question(
                    this,
                    QCoreApplication::translate("Editor.EditorDocumentWidget", "Remove AIX entry"),
                    QCoreApplication::translate("Editor.EditorDocumentWidget", "Remove AIX %1 %2?")
                        .arg(removing_segment ? QCoreApplication::translate("Editor.EditorDocumentWidget", "segment") : QCoreApplication::translate("Editor.EditorDocumentWidget", "layer"))
                        .arg(selection->index)) != QMessageBox::Yes) {
                return;
            }
            auto result = removing_segment
                ? m_aix->remove_segment(static_cast<size_t>(selection->index))
                : m_aix->remove_layer(static_cast<size_t>(selection->index));
            if (!result) {
                QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Remove failed"), utf8_to_qstring(result.error()));
                return;
            }
            const auto count = removing_segment ? m_aix->segments().size() : m_aix->layers().size();
            finish_transform_entry_edit(
                QCoreApplication::translate("Editor.EditorDocumentWidget", "Remove AIX entry"),
                (std::min)(selection->index, static_cast<int>(count) - 1),
                selection->payload_kind);
            return;
        }

        const auto index = selected_editable_transform_index();
        if (!index || QMessageBox::question(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Remove entry"),
                QCoreApplication::translate("Editor.EditorDocumentWidget", "Remove the selected entry?")) != QMessageBox::Yes) {
            return;
        }
        auto result = m_transform_kind == TransformKind::Aax
            ? m_aax->remove_segment(static_cast<uint32_t>(*index))
            : m_csb->remove_file(static_cast<uint32_t>(*index));
        if (!result) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Remove failed"), utf8_to_qstring(result.error()));
            return;
        }
        const auto count = m_transform_kind == TransformKind::Aax ? m_aax->segment_count() : m_csb->stream_count();
        finish_transform_entry_edit(QCoreApplication::translate("Editor.EditorDocumentWidget", "Remove entry"),
            (std::min)(*index, static_cast<int>(count) - 1));
    }

    void move_transform_entry(int delta) {
        if (m_transform_kind == TransformKind::Aix && m_aix) {
            const auto selection = selected_transform_edit_target();
            if (!selection ||
                (selection->payload_kind != modules::aix::segment_row_kind &&
                 selection->payload_kind != modules::aix::layer_row_kind)) {
                return;
            }
            const bool moving_segment = selection->payload_kind == modules::aix::segment_row_kind;
            const auto count = static_cast<int>(moving_segment ? m_aix->segments().size() : m_aix->layers().size());
            const auto destination = selection->index + delta;
            if (destination < 0 || destination >= count) {
                return;
            }
            auto result = moving_segment
                ? m_aix->move_segment(static_cast<size_t>(selection->index), static_cast<size_t>(destination))
                : m_aix->move_layer(static_cast<size_t>(selection->index), static_cast<size_t>(destination));
            if (!result) {
                QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Move failed"), utf8_to_qstring(result.error()));
                return;
            }
            finish_transform_entry_edit(
                QCoreApplication::translate("Editor.EditorDocumentWidget", "Move AIX entry"),
                destination,
                selection->payload_kind);
            return;
        }

        const auto index = selected_editable_transform_index();
        if (!index) {
            return;
        }
        const auto count = static_cast<int>(m_transform_kind == TransformKind::Aax
            ? m_aax->segment_count() : m_csb->stream_count());
        const auto destination = *index + delta;
        if (destination < 0 || destination >= count) {
            return;
        }
        auto result = m_transform_kind == TransformKind::Aax
            ? m_aax->move_segment(static_cast<uint32_t>(*index), static_cast<uint32_t>(destination))
            : m_csb->move_file(static_cast<uint32_t>(*index), static_cast<uint32_t>(destination));
        if (!result) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Move failed"), utf8_to_qstring(result.error()));
            return;
        }
        finish_transform_entry_edit(QCoreApplication::translate("Editor.EditorDocumentWidget", "Move entry"), destination);
    }

    void rename_transform_entry() {
        const auto index = selected_editable_transform_index();
        if (!index || m_transform_kind != TransformKind::Csb) {
            return;
        }
        bool accepted = false;
        const auto current = path_to_qstring(m_csb->stream(static_cast<uint32_t>(*index)).suggested_path());
        const auto name = QInputDialog::getText(
            this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Rename CSB stream"), QCoreApplication::translate("Editor.EditorDocumentWidget", "Archive path"),
            QLineEdit::Normal, current, &accepted).trimmed();
        if (!accepted || name.isEmpty()) {
            return;
        }
        auto result = m_csb->rename_file(static_cast<uint32_t>(*index), path_from_qstring(name));
        if (!result) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Rename failed"), utf8_to_qstring(result.error()));
            return;
        }
        finish_transform_entry_edit(QCoreApplication::translate("Editor.EditorDocumentWidget", "Rename entry"), *index);
    }

    void toggle_transform_entry_flag() {
        if (!m_ui.transform_table->currentIndex().isValid()) {
            return;
        }
        const auto* detail = m_ui.transform_model->detail_at(m_ui.transform_table->currentIndex().row());
        if (detail == nullptr) {
            return;
        }
        const auto payload_kind = detail->payload_kind;
        const auto index = detail->index;
        std::expected<void, std::string> result = std::unexpected(cristudio::i18n::translate_utf8("Editor.EditorDocumentWidget", "Select an editable entry first"));
        int selection_kind = payload_kind;
        int selection_index = index;
        if (m_transform_kind == TransformKind::Aax && payload_kind == 1 && index >= 0) {
            result = m_aax->set_loop_segment(
                static_cast<uint32_t>(index),
                !m_aax->segment(static_cast<uint32_t>(index)).loop_segment);
        } else if (m_transform_kind == TransformKind::Csb && payload_kind == 10 && index >= 0) {
            result = m_csb->set_element_streamed(
                static_cast<uint32_t>(index),
                !m_csb->element(static_cast<uint32_t>(index)).streamed);
        } else if (m_transform_kind == TransformKind::Csb && payload_kind == 5 && index >= 0) {
            const auto row_index = m_csb->stream(static_cast<uint32_t>(index)).row_index;
            for (uint32_t element_index = 0; element_index < m_csb->element_count(); ++element_index) {
                if (m_csb->element(element_index).row_index == row_index) {
                    selection_kind = 10;
                    selection_index = static_cast<int>(element_index);
                    result = m_csb->set_element_streamed(element_index, true);
                    break;
                }
            }
        }
        if (!result) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Edit flag failed"), utf8_to_qstring(result.error()));
            return;
        }
        finish_transform_entry_edit(QCoreApplication::translate("Editor.EditorDocumentWidget", "Edit entry flag"), selection_index, selection_kind);
    }

    void handle_archive_item_changed(QTableWidgetItem* item) {
        if (m_refreshing_archive || item == nullptr) {
            return;
        }
        const auto edited = edit_archive_table_item(
            mutable_archive_view(),
            item->row(),
            item->column(),
            item->column() == 12 && (item->flags() & Qt::ItemIsUserCheckable)
                ? (item->checkState() == Qt::Checked ? QStringLiteral("yes") : QStringLiteral("no"))
                : item->text()
        );
        const bool refresh = m_archive_kind != ArchiveKind::Cpk || item->column() != 12;
        apply_archive_edit_result(edited, refresh);
    }

    void apply_archive_edit_result(const ArchiveItemEditResult& edited, bool refresh = true) {
        if (!edited.handled) {
            return;
        }
        if (!edited.error.isEmpty()) {
            QMessageBox::warning(this, edited.warning_title, edited.error);
            refresh_archive_view();
            return;
        }
        if (edited.changed) {
            mark_archive_changed(edited.change_message, refresh);
        }
        if (edited.selected_row >= 0 && edited.selected_row < m_ui.archive_table->rowCount()) {
            m_ui.archive_table->setCurrentCell(edited.selected_row, 0);
            m_ui.archive_table->selectRow(edited.selected_row);
        }
    }

    void mark_utf_changed(QString message) {
        if (!m_utf) {
            return;
        }
        m_bytes = modules::utf::build_session_bytes(*m_utf);
        if (m_transform_kind == TransformKind::Acb) {
            m_acb_semantic_state_stale = true;
        }
        m_dirty = true;
        append_log(std::move(message));
        populate_info();
        refresh_utf_view();
        refresh_title();
    }

    void apply_utf_edit_result(const modules::utf::UtfEditResult& result) {
        if (!result.handled) {
            return;
        }
        if (result.show_cell) {
            show_selected_utf_cell(result.row, result.column);
            return;
        }
        if (!result.error.isEmpty()) {
            QMessageBox::warning(this, result.warning_title, result.error);
            if (result.refresh) {
                refresh_utf_view();
            }
            return;
        }
        if (!result.title.isEmpty()) {
            m_title = result.title;
        }
        if (result.changed) {
            mark_utf_changed(result.change_message);
        }
    }

    void mark_archive_changed(QString message, bool refresh = true) {
        m_dirty = true;
        append_log(std::move(message));
        populate_info();
        if (refresh) {
            refresh_archive_view();
        }
        refresh_title();
    }

    void set_all_cpk_compression(bool enabled) {
        if (m_archive_kind != ArchiveKind::Cpk || !m_cpk) {
            return;
        }
        modules::cpk::set_all_request_compress(*m_cpk, enabled);
        const QSignalBlocker signal_blocker(m_ui.archive_table);
        const bool updates_enabled = m_ui.archive_table->updatesEnabled();
        m_ui.archive_table->setUpdatesEnabled(false);
        for (int row = 0; row < m_ui.archive_table->rowCount(); ++row) {
            if (auto* item = m_ui.archive_table->item(row, 12); item != nullptr) {
                item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
            }
        }
        m_ui.archive_table->setUpdatesEnabled(updates_enabled);
        mark_archive_changed(
            enabled
                ? QCoreApplication::translate("Editor.EditorDocumentWidget", "Enabled save-time compression for all CPK entries.")
                : QCoreApplication::translate("Editor.EditorDocumentWidget", "Disabled save-time compression for all CPK entries."),
            false);
    }

    void apply_table_name() {
        if (!m_utf) {
            return;
        }
        apply_utf_edit_result(modules::utf::rename_table(*m_utf, m_ui.table_name_edit->text()));
    }

    void apply_selected_utf_value() {
        if (!m_utf) {
            return;
        }
        const auto [row, column] = utf_source_cell(
            m_ui.utf_grid->currentRow(), m_ui.utf_grid->currentColumn());
        apply_utf_edit_result(modules::utf::set_cell_value(
            *m_utf,
            row,
            column,
            m_ui.value_edit->text()
        ));
    }

    void handle_utf_grid_item_changed(QTableWidgetItem* item) {
        if (m_refreshing_utf || item == nullptr || !m_utf) {
            return;
        }
        if (utf_transposed() && item->column() != 2) {
            return;
        }
        const auto [row, column] = utf_source_cell(item->row(), item->column());
        apply_utf_edit_result(modules::utf::edit_grid_item(*m_utf, row, column, item->text()));
    }

    void handle_schema_item_changed(QTableWidgetItem* item) {
        if (m_refreshing_utf || item == nullptr || !m_utf) {
            return;
        }
        apply_utf_edit_result(modules::utf::edit_schema_item(*m_utf, item->row(), item->column(), item->text()));
    }

    void add_utf_row() {
        if (!m_utf) {
            return;
        }
        apply_utf_edit_result(modules::utf::add_row_action(*m_utf));
    }

    void remove_selected_utf_row() {
        if (!m_utf || m_ui.utf_grid->currentRow() < 0) {
            return;
        }
        const auto row = static_cast<uint32_t>(utf_transposed() ? 0 : m_ui.utf_grid->currentRow());
        if (QMessageBox::question(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Remove UTF row"), QCoreApplication::translate("Editor.EditorDocumentWidget", "Remove row %1?").arg(row)) != QMessageBox::Yes) {
            return;
        }
        apply_utf_edit_result(modules::utf::remove_row(*m_utf, static_cast<int>(row)));
    }

    void add_utf_column() {
        if (!m_utf) {
            return;
        }
        bool ok = false;
        const auto name = QInputDialog::getText(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Add UTF column"), QCoreApplication::translate("Editor.EditorDocumentWidget", "Column name"), QLineEdit::Normal, QCoreApplication::translate("Editor.EditorDocumentWidget", "Column"), &ok);
        if (!ok || name.trimmed().isEmpty()) {
            return;
        }
        const QStringList types{
            QStringLiteral("string"), QStringLiteral("binary"), QStringLiteral("u32"), QStringLiteral("u64"),
            QStringLiteral("s32"), QStringLiteral("float"), QStringLiteral("double"), QStringLiteral("guid"),
            QStringLiteral("u8"), QStringLiteral("s8"), QStringLiteral("u16"), QStringLiteral("s16"), QStringLiteral("s64")
        };
        const auto type_text = QInputDialog::getItem(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Add UTF column"), QCoreApplication::translate("Editor.EditorDocumentWidget", "Type"), types, 0, false, &ok);
        if (!ok) {
            return;
        }
        apply_utf_edit_result(modules::utf::add_column(*m_utf, name, type_text));
    }

    void remove_selected_utf_column() {
        if (!m_utf) {
            return;
        }
        int column = -1;
        if (m_ui.utf_grid->currentColumn() >= 0) {
            column = utf_transposed() ? m_ui.utf_grid->currentRow() : m_ui.utf_grid->currentColumn();
        } else if (m_ui.schema_table->currentRow() >= 0) {
            column = m_ui.schema_table->currentRow();
        }
        if (column < 0 || column >= static_cast<int>(m_utf->column_count())) {
            return;
        }
        const auto name = utf8_to_qstring(m_utf->column(static_cast<uint32_t>(column)).name);
        if (QMessageBox::question(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Remove UTF column"), QCoreApplication::translate("Editor.EditorDocumentWidget", "Remove column %1?").arg(name)) != QMessageBox::Yes) {
            return;
        }
        apply_utf_edit_result(modules::utf::remove_column(*m_utf, column));
    }

    void rename_selected_utf_column() {
        if (!m_utf || m_ui.utf_grid->currentColumn() < 0) {
            return;
        }
        const auto column = static_cast<uint32_t>(utf_transposed()
            ? m_ui.utf_grid->currentRow()
            : m_ui.utf_grid->currentColumn());
        bool ok = false;
        const auto current = utf8_to_qstring(m_utf->column(column).name);
        const auto name = QInputDialog::getText(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Rename UTF column"), QCoreApplication::translate("Editor.EditorDocumentWidget", "Column name"), QLineEdit::Normal, current, &ok);
        if (!ok || name.trimmed().isEmpty()) {
            return;
        }
        apply_utf_edit_result(modules::utf::rename_column(*m_utf, static_cast<int>(column), name));
    }

    void preview_entry_bytes(const EntrySummary& summary) {
        m_ui.replace_binary_button->setEnabled(false);
        if (!summary.has_source) {
            show_detail_preview(summary.detail.empty()
                ? QCoreApplication::translate("Editor.EditorDocumentWidget", "No binary source is attached to this row.")
                : editor_label(summary.detail));
            return;
        }

        if (m_utf && summary.source_format == "UTF") {
            auto bytes = utf_cell_bytes(summary.source_index);
            if (bytes) {
                show_hex_preview(summary, *bytes);
                m_ui.replace_binary_button->setEnabled(true);
                return;
            }
            show_detail_preview(QCoreApplication::translate("Editor.EditorDocumentWidget", "Error: UTF cell preview failed: %1").arg(bytes.error()));
            return;
        }

        if (summary.has_source) {
            if (auto bytes = load_embedded_entry_bytes(summary, m_request.keys)) {
                if (auto format = direct_audio_format(*bytes, &summary)) {
                    LoadedDocument document;
                    document.path = summary.source_path;
                    document.display_name = summary.name;
                    document.format = std::move(*format);
                    start_audio_preview(std::move(document), std::move(*bytes));
                    return;
                }
                show_hex_preview(summary, *bytes);
            } else {
                show_detail_preview(QCoreApplication::translate("Editor.EditorDocumentWidget", "Error: Binary preview failed: %1").arg(utf8_to_qstring(bytes.error())));
            }
        }
    }

    std::expected<std::vector<uint8_t>, QString> utf_cell_bytes(uint32_t source_index) const {
        if (!m_utf) {
            return std::unexpected(QCoreApplication::translate("Editor.EditorDocumentWidget", "UTF object is not loaded"));
        }
        auto data = cell_bytes(*m_utf, source_index);
        if (!data) {
            return std::unexpected(utf8_to_qstring(data.error()));
        }
        return *data;
    }

    void replace_selected_binary() {
        if (!m_utf) {
            return;
        }
        const auto [row, col] = utf_source_cell(
            m_ui.utf_grid->currentRow(), m_ui.utf_grid->currentColumn());
        if (row < 0 || col < 0 ||
            row >= static_cast<int>(m_utf->row_count()) ||
            col >= static_cast<int>(m_utf->column_count()) ||
            m_utf->column(static_cast<uint32_t>(col)).type != cricodecs::utf::ColumnType::VLData) {
            QMessageBox::information(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "No binary cell selected"), QCoreApplication::translate("Editor.EditorDocumentWidget", "Select a UTF binary/VLData cell first."));
            return;
        }
        const auto path_text = QFileDialog::getOpenFileName(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Choose replacement binary"));
        if (path_text.isEmpty()) {
            return;
        }
        auto bytes = read_file_bytes(path_from_qstring(path_text));
        if (!bytes) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Replacement failed"), bytes.error());
            return;
        }

        apply_utf_edit_result(modules::utf::replace_binary_cell(*m_utf, row, col, std::move(*bytes)));
    }

    void add_archive_file() {
        apply_archive_edit_result(cristudio::add_archive_file(this, mutable_archive_view()));
    }

    void replace_selected_archive_file() {
        const auto index = selected_archive_index();
        if (index < 0) {
            return;
        }
        apply_archive_edit_result(cristudio::replace_archive_file(this, mutable_archive_view(), index));
    }

    void remove_selected_archive_file() {
        const auto index = selected_archive_index();
        if (index < 0) {
            return;
        }
        apply_archive_edit_result(cristudio::remove_archive_file(this, mutable_archive_view(), index));
    }

    void move_selected_archive_entry(int delta) {
        const auto index = selected_archive_index();
        apply_archive_edit_result(cristudio::move_archive_entry(mutable_archive_view(), index, delta));
    }

    void rename_selected_archive_entry() {
        const auto index = selected_archive_index();
        if (index < 0 || m_archive_kind != ArchiveKind::Afs || !m_afs) {
            return;
        }
        const auto current = m_afs->entry(static_cast<uint32_t>(index)).name.value_or(std::string{});
        bool ok = false;
        const auto name = QInputDialog::getText(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Rename AFS entry"), QCoreApplication::translate("Editor.EditorDocumentWidget", "Entry name"), QLineEdit::Normal, utf8_to_qstring(current), &ok);
        if (!ok) {
            return;
        }
        const auto name_text = qstring_to_utf8(name.trimmed());
        auto result = modules::afs::rename_file(
            *m_afs,
            static_cast<uint32_t>(index),
            name_text.empty() ? std::nullopt : std::optional<std::string>(name_text)
        );
        if (!result) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Rename failed"), utf8_to_qstring(result.error()));
            return;
        }
        mark_archive_changed(QCoreApplication::translate("Editor.EditorDocumentWidget", "Renamed AFS entry %1.").arg(index));
    }

    void reserve_afs_file_id() {
        if (m_archive_kind != ArchiveKind::Afs || !m_afs) {
            return;
        }

        auto id = modules::afs::choose_reserve_file_id(this, *m_afs);
        if (!id) {
            return;
        }
        modules::afs::reserve_file_id(*m_afs, *id);
        mark_archive_changed(QCoreApplication::translate("Editor.EditorDocumentWidget", "Reserved AFS file ID %1.").arg(*id));
        const auto row = static_cast<int>(*id);
        if (row >= 0 && row < m_ui.archive_table->rowCount()) {
            m_ui.archive_table->setCurrentCell(row, 0);
            m_ui.archive_table->selectRow(row);
        }
    }

    void set_selected_afs_timestamp() {
        const auto index = selected_archive_index();
        if (index < 0 || m_archive_kind != ArchiveKind::Afs || !m_afs) {
            return;
        }

        auto selected = modules::afs::choose_directory_timestamp(
            this,
            m_afs->entry(static_cast<uint32_t>(index)),
            static_cast<uint32_t>(index)
        );
        if (!selected) {
            return;
        }

        auto result = modules::afs::set_directory_timestamp(*m_afs, static_cast<uint32_t>(index), *selected);
        if (!result) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Timestamp edit failed"), utf8_to_qstring(result.error()));
            return;
        }
        mark_archive_changed(QCoreApplication::translate("Editor.EditorDocumentWidget", "Updated AFS timestamp for file ID %1.").arg(index));
    }

    void set_selected_archive_wave_id() {
        const auto index = selected_archive_index();
        if (index < 0 || m_archive_kind != ArchiveKind::Awb || !m_awb) {
            return;
        }
        auto wave_id = modules::awb::choose_wave_id(this, *m_awb, static_cast<uint32_t>(index));
        if (!wave_id) {
            return;
        }
        auto result = modules::awb::set_wave_id(*m_awb, static_cast<uint32_t>(index), *wave_id);
        if (!result) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Wave ID edit failed"), utf8_to_qstring(result.error()));
            return;
        }
        mark_archive_changed(QCoreApplication::translate("Editor.EditorDocumentWidget", "Set AWB wave ID at index %1 to %2.").arg(index).arg(*wave_id));
    }

    void assign_awb_wave_ids() {
        if (m_archive_kind != ArchiveKind::Awb || !m_awb) {
            return;
        }

        auto options = modules::awb::choose_batch_wave_ids(this);
        if (!options) {
            return;
        }

        if (auto result = modules::awb::assign_wave_ids(*m_awb, options->start, options->step); !result) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Wave ID edit failed"), utf8_to_qstring(result.error()));
            refresh_archive_view();
            return;
        }
        mark_archive_changed(QCoreApplication::translate("Editor.EditorDocumentWidget", "Assigned AWB wave IDs from %1 with step %2.").arg(options->start).arg(options->step));
    }

    void edit_archive_entry_properties() {
        const auto row = selected_archive_index();
        if (row < 0) {
            return;
        }
        apply_archive_edit_result(cristudio::edit_archive_entry_properties(this, mutable_archive_view(), row));
    }

    void edit_archive_options() {
        apply_archive_edit_result(cristudio::edit_archive_options(this, mutable_archive_view()));
    }

    void import_afs_als_script() {
        if (m_archive_kind != ArchiveKind::Afs) {
            return;
        }
        auto options = modules::afs::choose_als_import_options(this, m_afs ? &*m_afs : nullptr);
        if (!options) {
            return;
        }

        auto imported = modules::afs::import_als(
            options->file_list_path,
            options->alignment,
            options->directory_table_enabled,
            options->source_root
        );
        if (!imported) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "ALS import failed"), utf8_to_qstring(imported.error()));
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "AFS ALS import failed: %1").arg(utf8_to_qstring(imported.error())));
            return;
        }

        m_afs = std::move(*imported);
        m_archive_kind = ArchiveKind::Afs;
        const auto path_text = path_to_qstring(options->file_list_path);
        m_title = QFileInfo(path_text).completeBaseName() + QStringLiteral(".afs");
        auto built = modules::afs::build_session_bytes(*m_afs);
        if (built) {
            m_bytes = std::move(*built);
        } else {
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "AFS ALS import build preview failed: %1").arg(utf8_to_qstring(built.error())));
            m_bytes.clear();
        }
        mark_archive_changed(QCoreApplication::translate("Editor.EditorDocumentWidget", "Imported AFS file list %1.").arg(path_text));
    }

    void export_afs_file_id_header() {
        if (m_archive_kind != ArchiveKind::Afs || !m_afs) {
            return;
        }

        auto default_archive_name = m_last_save_path.empty()
            ? path_to_qstring(m_request.source_path.filename())
            : path_to_qstring(m_last_save_path.filename());
        if (default_archive_name.isEmpty()) {
            default_archive_name = safe_output_name(m_title, QStringLiteral(".afs"));
        }
        auto exported = modules::afs::export_file_id_header(this, *m_afs, default_archive_name);
        if (!exported) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "AFS header export failed"), exported.error());
            append_log(exported.error());
            return;
        }
        if (!*exported) {
            return;
        }
        append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Exported AFS file-ID header to %1.").arg(path_to_qstring(**exported)));
    }

    void import_cvm_script() {
        if (m_archive_kind != ArchiveKind::Cvm) {
            return;
        }
        auto script_path = modules::cvm::choose_import_script(this);
        if (!script_path) {
            return;
        }

        auto imported = modules::cvm::import_build_script(*script_path);
        if (!imported) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "CVS import failed"), utf8_to_qstring(imported.error()));
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "CVM CVS import failed: %1").arg(utf8_to_qstring(imported.error())));
            return;
        }

        m_bytes = std::move(imported->bytes);
        m_cvm = std::move(imported->container);
        m_archive_kind = ArchiveKind::Cvm;
        mark_archive_changed(QCoreApplication::translate("Editor.EditorDocumentWidget", "Imported CVM build script %1.").arg(path_to_qstring(*script_path)));
    }

    void export_cvm_script() {
        if (m_archive_kind != ArchiveKind::Cvm || !m_cvm) {
            return;
        }
        auto output_path = modules::cvm::choose_export_script(this, m_title);
        if (!output_path) {
            return;
        }

        auto result = modules::cvm::export_build_script(*m_cvm, *output_path);
        if (!result) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "CVS export failed"), utf8_to_qstring(result.error()));
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "CVM CVS export failed: %1").arg(utf8_to_qstring(result.error())));
            return;
        }
        append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Exported CVM build script to %1.").arg(path_to_qstring(*output_path)));
    }

    void extract_selected_archive_entry(bool raw_mode) {
        const auto index = selected_archive_index();
        if (index < 0) {
            return;
        }
        const auto view = archive_view();
        const auto default_name = archive_entry_default_name(view, static_cast<uint32_t>(index));
        const auto path_text = QFileDialog::getSaveFileName(
            this,
            raw_mode ? QCoreApplication::translate("Editor.EditorDocumentWidget", "Extract raw archive entry") : QCoreApplication::translate("Editor.EditorDocumentWidget", "Extract archive entry"),
            default_name
        );
        if (path_text.isEmpty()) {
            return;
        }
        auto bytes = archive_entry_bytes(view, static_cast<uint32_t>(index), m_preview_scratch);
        if (!bytes) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Extract failed"), bytes.error());
            return;
        }
        std::vector<uint8_t> raw_bytes;
        std::span<const uint8_t> output_bytes = *bytes;
        if (raw_mode) {
            auto transformed = raw_extract_bytes(*bytes, m_request.keys);
            if (!transformed) {
                QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Extract raw failed"), utf8_to_qstring(transformed.error()));
                append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Raw archive entry extract failed: %1").arg(utf8_to_qstring(transformed.error())));
                return;
            }
            raw_bytes = std::move(*transformed);
            output_bytes = std::span<const uint8_t>(raw_bytes.data(), raw_bytes.size());
        }
        auto result = write_file_bytes(path_from_qstring(path_text), output_bytes);
        if (!result) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Extract failed"), result.error());
            return;
        }
        append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "%1 archive entry %2 to %3.")
            .arg(raw_mode ? QCoreApplication::translate("Editor.EditorDocumentWidget", "Extracted raw") : QCoreApplication::translate("Editor.EditorDocumentWidget", "Extracted"))
            .arg(index)
            .arg(path_text));
    }

    QString transform_default_suffix() const {
        switch (m_transform_kind) {
        case TransformKind::AudioEncode:
            return QStringLiteral(".adx");
        case TransformKind::MediaBuild:
            return QStringLiteral(".usm");
        case TransformKind::Adx:
            return (m_adx && m_adx->is_ahx()) ? QStringLiteral(".ahx") : QStringLiteral(".adx");
        case TransformKind::Hca:
            return QStringLiteral(".hca");
        case TransformKind::Aax:
            return QStringLiteral(".aax");
        case TransformKind::Aix:
            return QStringLiteral(".aix");
        case TransformKind::Usm:
            return QStringLiteral(".usm");
        case TransformKind::Sfd:
            return QStringLiteral(".sfd");
        case TransformKind::Csb:
            return QStringLiteral(".csb");
        case TransformKind::Acb:
            return QStringLiteral(".acb");
        case TransformKind::None:
            break;
        }
        return QStringLiteral(".bin");
    }

    void start_transform_file_job(
        QString label,
        std::filesystem::path path,
        std::future<std::expected<void, QString>> future,
        std::shared_ptr<BuildJobLog> log = {}
    ) {
        if (save_running()) {
            return;
        }
        m_last_save_path = std::move(path);
        m_active_job_log = std::move(log);
        m_active_job_label = label;
        m_active_job_saves_document = false;
        m_save_future = std::move(future);
        update_header_actions();
        m_ui.progress->show();
        append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "%1 started: %2").arg(std::move(label)).arg(path_to_qstring(m_last_save_path)));
        m_poll_timer.start(50, this);
    }

    void open_audio_encode_wizard() {
        if (save_running() || (m_transform_kind != TransformKind::AudioEncode &&
                               m_transform_kind != TransformKind::Adx &&
                               m_transform_kind != TransformKind::Hca)) {
            return;
        }

        auto selected = modules::audio::choose_encode_config(
            this,
            m_title,
            m_request.keys,
            m_transform_kind == TransformKind::Hca ? modules::audio::EncodeTarget::Hca : modules::audio::EncodeTarget::Adx
        );
        if (!selected) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Encode from WAV"), selected.error());
            return;
        }
        if (!*selected) {
            return;
        }
        auto config = std::move(**selected);
        m_apply_job_output_path = config.output_path;

        auto log = std::make_shared<BuildJobLog>();
        start_transform_file_job(
            config.target == modules::audio::EncodeTarget::Hca ? QCoreApplication::translate("Editor.EditorDocumentWidget", "HCA encode") : QCoreApplication::translate("Editor.EditorDocumentWidget", "ADX/AHX encode"),
            config.output_path,
            std::async(std::launch::async, [config, log] {
                return modules::audio::encode_from_wav(config, [log](QString message) {
                    push_job_log(log, std::move(message));
                });
            }),
            log
        );
    }

    void open_acb_associated_awb() {
        if (m_transform_kind != TransformKind::Acb || !m_acb || m_tabs == nullptr) {
            return;
        }
        auto awb = modules::acb::prepare_associated_awb_open(this, *m_acb, m_title, m_request.keys, m_request.source_path);
        if (!awb) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Open AWB failed"), awb.error());
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "ACB associated AWB open failed: %1").arg(awb.error()));
            return;
        }

        EditorOpenRequest request;
        request.source_kind = EditorOpenRequest::SourceKind::Path;
        request.display_name = std::move(awb->display_name);
        request.detected_format = "AWB";
        request.keys = std::move(awb->keys);
        request.source_path = std::move(awb->source_path);
        request.source_archive_path = std::move(awb->source_archive_path);
        request.source_archive_format = "ACB";
        request.source_bytes = std::move(awb->bytes);

        auto* document = new EditorDocumentWidget(std::move(request), m_tabs);
        const auto index = m_tabs->addTab(document, document->tab_title());
        m_tabs->setCurrentIndex(index);
        append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Opened associated AWB in a new Editor tab."));
    }

    void export_acb_associated_awb() {
        if (m_transform_kind != TransformKind::Acb || !m_acb || save_running()) {
            return;
        }
        auto export_payload = modules::acb::choose_associated_awb_export(this, *m_acb, m_title);
        if (!export_payload) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Export AWB failed"), export_payload.error());
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "ACB associated AWB export failed: %1").arg(export_payload.error()));
            return;
        }
        if (!*export_payload) {
            return;
        }
        auto payload = std::move(**export_payload);
        start_transform_file_job(
            QCoreApplication::translate("Editor.EditorDocumentWidget", "ACB AWB export"),
            payload.output_path,
            std::async(std::launch::async, [payload = std::move(payload)]() mutable {
                return write_file_bytes(payload.output_path, payload.bytes);
            })
        );
    }

    bool open_media_build_wizard() {
        if (save_running()) {
            return false;
        }

        const bool has_current_usm = m_transform_kind == TransformKind::Usm && m_usm.has_value();
        const auto current_usm_bytes = has_current_usm
            ? std::span<const uint8_t>(m_bytes.data(), m_bytes.size())
            : std::span<const uint8_t>{};
        auto selected = modules::usm::choose_media_build_config(
            this,
            m_title,
            m_request.keys,
            m_transform_kind == TransformKind::Sfd || m_request.media_build_prefer_sfd,
            has_current_usm ? &*m_usm : nullptr,
            std::filesystem::path{},
            current_usm_bytes
        );
        if (!selected) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Build wizard"), selected.error());
            return false;
        }
        if (!*selected) {
            return false;
        }
        auto config = std::move(**selected);
        m_apply_job_output_path = config.apply_to_editor
            ? std::optional<std::filesystem::path>(config.output_path)
            : std::nullopt;

        auto log = std::make_shared<BuildJobLog>();
        auto output_path = config.output_path;
        start_transform_file_job(
            config.target == modules::usm::MediaBuildTarget::Sfd ? QCoreApplication::translate("Editor.EditorDocumentWidget", "SFD build wizard") : QCoreApplication::translate("Editor.EditorDocumentWidget", "USM build wizard"),
            output_path,
            std::async(std::launch::async, [config = std::move(config), log] () mutable {
                return modules::usm::build_media_from_sources(std::move(config), [log](QString message) {
                    push_job_log(log, std::move(message));
                });
            }),
            log
        );
        return true;
    }

    void set_editor_video_visible(bool visible) {
        m_ui.video.frame->setVisible(visible);
        m_ui.video.widget->setVisible(visible);
    }

    void clear_editor_preview_files() {
        if (m_editor_media_player != nullptr) {
            m_editor_media_player->stop();
            m_editor_media_player->setVideoOutput(nullptr);
            m_editor_media_player->setSource({});
        }
        set_editor_video_visible(false);
        m_ui.video.widget->clearFocus();
        m_editor_slider_dragging = false;
        m_editor_resume_after_seek = false;
        m_ui.media.seek_slider->setRange(0, 0);
        m_ui.media.seek_slider->setValue(0);
        m_ui.media.seek_slider->setEnabled(false);
        m_ui.media.time_label->setText(QStringLiteral("0:00 / 0:00"));
        m_ui.media.play_button->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        m_ui.media.play_button->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Play"));
        m_ui.media.play_button->setEnabled(false);
        m_editor_audio_loops.clear();
        m_editor_audio_sample_rate = 0;
        m_editor_loop_seeking = false;
        m_ui.media.loop_list->clear();
        const QSignalBlocker loop_blocker(m_ui.media.loop_toggle);
        const QSignalBlocker audio_blocker(m_ui.media.audio_combo);
        const QSignalBlocker subtitle_blocker(m_ui.media.subtitle_combo);
        m_ui.media.loop_toggle->setChecked(false);
        m_ui.media.loop_row->hide();
        m_ui.media.audio_combo->clear();
        m_ui.media.subtitle_combo->clear();
        m_ui.media.audio_row->hide();
        m_ui.media.subtitle_row->hide();
        if (m_editor_preview_temp_dir.empty()) {
            return;
        }
        std::error_code ec;
        std::filesystem::remove_all(m_editor_preview_temp_dir, ec);
        m_editor_preview_temp_dir.clear();
    }

    void dismiss_editor_media_preview() {
        ++m_audio_preview_request_id;
        ++m_prepared_media_request_id;
        m_pending_audio_preview.reset();
        clear_editor_preview_files();
        m_ui.mux_preview_panel->hide();
    }

    void ensure_editor_media_player() {
        if (m_editor_media_player != nullptr) {
            return;
        }
        m_editor_media_player = new QMediaPlayer(this);
        m_editor_audio_output = new QAudioOutput(this);
        m_editor_audio_output->setVolume(static_cast<float>(
            std::clamp(m_ui.media.volume_slider->value(), 0, 100)) / 100.0f);
        m_editor_media_player->setAudioOutput(m_editor_audio_output);
        connect(m_editor_media_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
            m_ui.media.seek_slider->setRange(0, static_cast<int>(std::clamp<qint64>(
                duration, 0, (std::numeric_limits<int>::max)())));
            update_editor_media_time();
        });
        connect(m_editor_media_player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
            if (!m_editor_slider_dragging) {
                QSignalBlocker blocker(m_ui.media.seek_slider);
                m_ui.media.seek_slider->setValue(static_cast<int>(std::clamp<qint64>(
                    position, 0, (std::numeric_limits<int>::max)())));
            }
            handle_editor_audio_loop(position);
            update_editor_media_time();
        });
        connect(m_editor_media_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                m_ui.media.play_button->setIcon(style()->standardIcon(
                    state == QMediaPlayer::PlayingState ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
                m_ui.media.play_button->setText(
                    state == QMediaPlayer::PlayingState ? QCoreApplication::translate("Editor.EditorDocumentWidget", "Pause") : QCoreApplication::translate("Editor.EditorDocumentWidget", "Play"));
            });
        connect(m_editor_media_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& message) {
                if (!message.isEmpty()) {
                    m_ui.media.status_label->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Playback error: ") + message);
                }
            });
    }

    void update_editor_media_time() {
        if (m_editor_media_player == nullptr) {
            return;
        }
        const auto position = m_editor_slider_dragging
            ? static_cast<qint64>(m_ui.media.seek_slider->value())
            : m_editor_media_player->position();
        m_ui.media.time_label->setText(
            time_text(position) + QStringLiteral(" / ") +
            time_text(m_editor_media_player->duration()));
    }

    void configure_editor_audio_loops(const AudioPreview& audio) {
        m_editor_audio_loops = audio.loops;
        m_editor_audio_sample_rate = audio.sample_rate;
        QSignalBlocker toggle_blocker(m_ui.media.loop_toggle);
        QSignalBlocker list_blocker(m_ui.media.loop_list);
        m_ui.media.loop_list->clear();
        const auto to_ms = [sample_rate = audio.sample_rate](uint64_t sample) -> qint64 {
            return sample_rate == 0 ? 0 : static_cast<qint64>((sample * 1000ull) / sample_rate);
        };
        for (size_t index = 0; index < m_editor_audio_loops.size(); ++index) {
            const auto& loop = m_editor_audio_loops[index];
            auto name = utf8_to_qstring(loop.name);
            if (name.isEmpty()) {
                name = QCoreApplication::translate("Editor.EditorDocumentWidget", "Loop %1").arg(index + 1);
            }
            auto* item = new QListWidgetItem(
                QCoreApplication::translate("Editor.EditorDocumentWidget", "%1    %2 - %3    samples %4 - %5")
                    .arg(name)
                    .arg(time_text(to_ms(loop.start_sample)))
                    .arg(time_text(to_ms(loop.end_sample)))
                    .arg(loop.start_sample)
                    .arg(loop.end_sample),
                m_ui.media.loop_list);
            item->setData(Qt::UserRole, static_cast<int>(index));
        }
        const auto has_loops = !m_editor_audio_loops.empty() && audio.sample_rate != 0;
        m_ui.media.loop_toggle->setChecked(false);
        m_ui.media.loop_toggle->setEnabled(has_loops);
        m_ui.media.loop_list->setEnabled(has_loops);
        m_ui.media.loop_row->setVisible(has_loops);
        if (has_loops) {
            m_ui.media.loop_list->setCurrentRow(0);
            const auto visible_rows = (std::min)(4, static_cast<int>(m_editor_audio_loops.size()));
            m_ui.media.loop_list->setFixedHeight((std::max)(36, 32 * visible_rows + 6));
        }
    }

    void handle_editor_audio_loop(qint64 position) {
        if (m_editor_media_player == nullptr || !m_ui.media.loop_toggle->isChecked() ||
            m_editor_loop_seeking || m_editor_slider_dragging || m_editor_audio_sample_rate == 0) {
            return;
        }
        const auto index = m_ui.media.loop_list->currentRow();
        if (index < 0 || index >= static_cast<int>(m_editor_audio_loops.size())) {
            return;
        }
        const auto& loop = m_editor_audio_loops[static_cast<size_t>(index)];
        const auto start = static_cast<qint64>((loop.start_sample * 1000ull) / m_editor_audio_sample_rate);
        const auto end = static_cast<qint64>((loop.end_sample * 1000ull) / m_editor_audio_sample_rate);
        if (end <= start || position < end) {
            return;
        }
        m_editor_loop_seeking = true;
        m_editor_media_player->setPosition(start);
        m_editor_loop_seeking = false;
    }

    void show_editor_playable_media() {
        m_ui.mux_preview_panel->show();
        m_ui.media.panel->show();
        m_ui.media.play_button->setEnabled(true);
        m_ui.media.play_button->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Play"));
        m_ui.media.seek_slider->setEnabled(true);
        set_preview_tabs(true, raw_preview_available(), 0);
    }

    void finish_current_audio_preview() {
        if (m_audio_preview_watcher == nullptr) {
            return;
        }
        auto result = m_audio_preview_watcher->future().takeResult();
        if (m_pending_audio_preview) {
            auto pending = std::move(*m_pending_audio_preview);
            m_pending_audio_preview.reset();
            launch_audio_preview(std::move(pending));
            return;
        }
        if (result.request_id != m_audio_preview_request_id) {
            return;
        }
        auto preview = std::move(result.preview);
        if (!preview) {
            m_ui.media.status_label->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Preview unavailable: %1").arg(utf8_to_qstring(preview.error())));
            m_ui.media.play_button->setEnabled(false);
            return;
        }

        clear_editor_preview_files();
        QTemporaryDir preview_dir(QDir::tempPath() + QStringLiteral("/CriStudio-editor-audio-preview-XXXXXX"));
        if (!preview_dir.isValid()) {
            m_ui.media.status_label->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Could not create a temporary audio preview directory."));
            m_ui.media.play_button->setEnabled(false);
            return;
        }
        const auto wav_path = path_from_qstring(preview_dir.filePath(QStringLiteral("preview.wav")));
        if (auto written = write_file_bytes(wav_path, preview->wav_bytes); !written) {
            m_ui.media.status_label->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Could not stage audio preview: %1").arg(written.error()));
            m_ui.media.play_button->setEnabled(false);
            return;
        }

        preview_dir.setAutoRemove(false);
        m_editor_preview_temp_dir = path_from_qstring(preview_dir.path());
        ensure_editor_media_player();
        m_editor_media_player->setVideoOutput(nullptr);
        m_editor_media_player->setSource(QUrl::fromLocalFile(path_to_qstring(wav_path)));
        configure_editor_audio_loops(*preview);
        set_editor_video_visible(false);
        m_ui.media.status_label->setText(QCoreApplication::translate(
            "Editor.EditorDocumentWidget",
            "%1 - %2 Hz, %n channel(s)",
            nullptr,
            static_cast<int>(preview->channels))
            .arg(utf8_to_qstring(preview->format))
            .arg(preview->sample_rate));
        show_editor_playable_media();
    }

    void present_video_preview(VideoPreview video) {
        if (video.playable_path.empty()) {
            show_detail_preview(QCoreApplication::translate("Editor.EditorDocumentWidget", "Preview unavailable: %1")
                .arg(utf8_to_qstring(video.note.empty() ? cristudio::i18n::translate_utf8("Editor.EditorDocumentWidget", "video preparation failed") : video.note)));
            return;
        }
        clear_editor_preview_files();
        m_editor_preview_temp_dir = video.temporary_directory;
        ensure_editor_media_player();
        m_editor_media_player->setVideoOutput(m_ui.video.widget);
        m_editor_media_player->setSource(QUrl::fromLocalFile(path_to_qstring(video.playable_path)));
        m_ui.payload_table->hide();
        set_editor_video_visible(true);
        m_ui.media.status_label->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "%1 video preview ready")
            .arg(utf8_to_qstring(video.format)));
        show_editor_playable_media();
    }

    void start_video_preview(VideoPreview video) {
        dismiss_editor_media_preview();
        set_raw_preview(video.video_bytes, video.video_bytes.size(), video.format);
        m_ui.payload_table->hide();
        m_ui.mux_preview_panel->show();
        set_editor_video_visible(false);
        m_ui.media.status_label->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Preparing video preview..."));
        m_ui.media.play_button->setEnabled(false);
        set_preview_tabs(true, raw_preview_available(), 0);

        const auto request_id = ++m_prepared_media_request_id;
        auto* watcher = new QFutureWatcher<EditorVideoPreviewResult>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
            auto result = watcher->future().takeResult();
            watcher->deleteLater();
            if (result.request_id != m_prepared_media_request_id) {
                remove_preview_temporary_directory(result.preview);
                return;
            }
            present_video_preview(std::move(result.preview));
        });
        watcher->setFuture(QtConcurrent::run([request_id, video = std::move(video)]() mutable {
            prepare_video_preview_for_playback(video);
            return EditorVideoPreviewResult{
                .request_id = request_id,
                .preview = std::move(video),
            };
        }));
    }

    void launch_audio_preview(EditorAudioPreviewRequest request) {
        const auto keys = m_request.keys;
        m_audio_preview_watcher->setFuture(QtConcurrent::run([
            request = std::move(request),
            keys
        ]() mutable {
            EditorAudioPreviewResult result;
            result.request_id = request.request_id;
            result.preview = audio_preview_from_bytes(request.document, request.bytes, keys);
            return result;
        }));
    }

    void start_audio_preview(LoadedDocument document, std::vector<uint8_t> bytes) {
        if (bytes.empty()) {
            return;
        }

        dismiss_editor_media_preview();
        clear_raw_preview();

        m_ui.preview_tabs->setCurrentIndex(0);
        m_ui.mux_preview_panel->show();
        set_editor_video_visible(false);
        m_ui.media.status_label->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Preparing audio preview..."));
        m_ui.media.play_button->setEnabled(false);
        m_ui.payload_table->hide();
        set_raw_preview(bytes, bytes.size(), std::string(document_format_id(document)));
        set_preview_tabs(true, raw_preview_available(), 0);

        if (m_audio_preview_watcher == nullptr) {
            m_audio_preview_watcher = new QFutureWatcher<EditorAudioPreviewResult>(this);
            connect(m_audio_preview_watcher, &QFutureWatcherBase::finished, this, [this] {
                finish_current_audio_preview();
            });
        }
        EditorAudioPreviewRequest request {
            .request_id = ++m_audio_preview_request_id,
            .document = std::move(document),
            .bytes = std::move(bytes),
        };
        if (m_audio_preview_watcher->isRunning()) {
            m_pending_audio_preview = std::move(request);
            return;
        }
        launch_audio_preview(std::move(request));
    }

    void preview_current_audio() {
        if (m_bytes.empty()) {
            return;
        }
        auto document = m_document.value_or(LoadedDocument{});
        if (document.format.empty()) {
            document.format = qstring_to_utf8(transform_kind_name(m_transform_kind));
        }
        start_audio_preview(std::move(document), m_bytes);
    }

    void preview_current_media() {
        if (m_transform_kind == TransformKind::Usm || m_transform_kind == TransformKind::Sfd) {
            preview_current_mux();
            return;
        }
        preview_current_audio();
    }

    void preview_current_mux(int audio_choice = 0) {
        const bool is_mux = m_transform_kind == TransformKind::Usm || m_transform_kind == TransformKind::Sfd;
        if (save_running() || !is_mux || m_bytes.empty()) {
            return;
        }

        dismiss_editor_media_preview();
        clear_raw_preview();

        m_ui.preview_tabs->setCurrentIndex(0);
        m_ui.mux_preview_panel->show();
        set_editor_video_visible(false);
        m_ui.media.status_label->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Preparing mux preview..."));
        m_ui.media.play_button->setEnabled(false);
        m_ui.payload_table->hide();
        QApplication::processEvents();

        QTemporaryDir preview_dir(QDir::tempPath() + QStringLiteral("/CriStudio-editor-mux-preview-XXXXXX"));
        if (!preview_dir.isValid()) {
            m_ui.media.status_label->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Could not create a temporary preview directory."));
            m_ui.mux_preview_panel->show();
            return;
        }
        const auto suffix = m_transform_kind == TransformKind::Sfd ? QStringLiteral(".sfd") : QStringLiteral(".usm");
        const auto source_path = path_from_qstring(preview_dir.filePath(QStringLiteral("editor-preview") + suffix));
        if (auto saved = write_file_bytes(source_path, m_bytes); !saved) {
            m_ui.media.status_label->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Could not stage editor bytes: %1").arg(saved.error()));
            m_ui.mux_preview_panel->show();
            return;
        }

        std::string reason;
        auto document = load_document_summary(source_path, reason, m_request.keys);
        if (!document) {
            m_ui.media.status_label->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Could not inspect editor mux: %1").arg(utf8_to_qstring(reason)));
            m_ui.mux_preview_panel->show();
            return;
        }
        preview_dir.setAutoRemove(false);
        const auto staging_directory = path_from_qstring(preview_dir.path());
        const auto request_id = ++m_prepared_media_request_id;
        auto* watcher = new QFutureWatcher<EditorMuxPreviewResult>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
            auto result = watcher->future().takeResult();
            watcher->deleteLater();
            if (result.request_id != m_prepared_media_request_id) {
                if (result.preview) {
                    remove_preview_temporary_directory(*result.preview);
                }
                return;
            }
            if (!result.preview) {
                m_ui.media.status_label->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Preview unavailable: %1")
                    .arg(utf8_to_qstring(result.preview.error())));
                m_ui.mux_preview_panel->show();
                return;
            }
            auto mux = std::move(*result.preview);
            set_raw_preview(mux.video_bytes, mux.video_bytes.size(), mux.format);
            if (mux.playable_path.empty()) {
                m_ui.media.status_label->setText(utf8_to_qstring(
                    mux.note.empty() ? cristudio::i18n::translate_utf8("Editor.EditorDocumentWidget", "Preview preparation failed") : mux.note));
                m_ui.mux_preview_panel->show();
                return;
            }
            clear_editor_preview_files();
            {
                QSignalBlocker blocker(m_ui.media.audio_combo);
                for (int index = 0; index < static_cast<int>(mux.audio_choices.size()); ++index) {
                    const auto& choice = mux.audio_choices[static_cast<size_t>(index)];
                    auto label = utf8_to_qstring(choice.name);
                    if (!choice.detail.empty()) {
                        label += QStringLiteral("  -  ") + utf8_to_qstring(choice.detail);
                    }
                    m_ui.media.audio_combo->addItem(label, index);
                }
                if (mux.selected_audio >= 0 && mux.selected_audio < m_ui.media.audio_combo->count()) {
                    m_ui.media.audio_combo->setCurrentIndex(mux.selected_audio);
                }
            }
            m_ui.media.audio_row->setVisible(m_ui.media.audio_combo->count() > 0);
            {
                QSignalBlocker blocker(m_ui.media.subtitle_combo);
                m_ui.media.subtitle_combo->addItem(QCoreApplication::translate("Editor.EditorDocumentWidget", "Disabled"), -1);
                for (int index = 0; index < static_cast<int>(mux.subtitle_choices.size()); ++index) {
                    const auto& choice = mux.subtitle_choices[static_cast<size_t>(index)];
                    auto label = utf8_to_qstring(choice.detail.empty() ? choice.name : choice.detail);
                    if (!choice.name.empty() && !choice.detail.empty()) {
                        label += QStringLiteral("  -  ") + utf8_to_qstring(choice.name);
                    }
                    m_ui.media.subtitle_combo->addItem(label, index);
                }
                m_ui.media.subtitle_combo->setCurrentIndex(
                    mux.selected_subtitle >= 0 && mux.selected_subtitle + 1 < m_ui.media.subtitle_combo->count()
                        ? mux.selected_subtitle + 1
                        : 0);
            }
            m_ui.media.subtitle_row->setVisible(!mux.subtitle_choices.empty());
            m_editor_preview_temp_dir = mux.temporary_directory;
            ensure_editor_media_player();
            m_editor_media_player->setVideoOutput(m_ui.video.widget);
            m_editor_media_player->setSource(QUrl::fromLocalFile(path_to_qstring(mux.playable_path)));
            m_editor_media_player->setActiveSubtitleTrack(m_ui.media.subtitle_combo->currentData().toInt());
            set_editor_video_visible(true);
            m_ui.media.status_label->setText(QCoreApplication::translate("Editor.EditorDocumentWidget", "Editor mux preview ready"));
            show_editor_playable_media();
        });
        const auto keys = m_request.keys;
        watcher->setFuture(QtConcurrent::run([
            request_id,
            document = std::move(*document),
            keys,
            staging_directory,
            audio_choice
        ]() mutable {
            auto preview = build_mux_preview(document, audio_choice, keys);
            if (preview) {
                prepare_mux_preview_for_playback(*preview);
            }
            std::error_code ec;
            std::filesystem::remove_all(staging_directory, ec);
            return EditorMuxPreviewResult{
                .request_id = request_id,
                .preview = std::move(preview),
            };
        }));
    }

    void open_adx_container_build_wizard() {
        if (save_running() || (m_transform_kind != TransformKind::Aax && m_transform_kind != TransformKind::Aix)) {
            return;
        }

        const bool aix_target = m_transform_kind == TransformKind::Aix;
        auto selected = modules::adx::choose_container_build_config(this, m_title, aix_target);
        if (!selected) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "ADX container build"), selected.error());
            return;
        }
        if (!*selected) {
            return;
        }

        auto log = std::make_shared<BuildJobLog>();
        if (auto* config = std::get_if<modules::aix::BuildConfig>(&**selected)) {
            auto output_path = config->output_path;
            start_transform_file_job(
                QCoreApplication::translate("Editor.EditorDocumentWidget", "AIX ADX build"),
                output_path,
                std::async(std::launch::async, [config = std::move(*config), log] () mutable {
                    return modules::aix::build_from_adx_segments(std::move(config), [log](QString message) {
                        push_job_log(log, std::move(message));
                    });
                }),
                log
            );
            return;
        }

        auto config = std::get<modules::aax::BuildConfig>(std::move(**selected));
        auto output_path = config.output_path;
        start_transform_file_job(
            QCoreApplication::translate("Editor.EditorDocumentWidget", "AAX ADX build"),
            output_path,
            std::async(std::launch::async, [config = std::move(config), log] () mutable {
                return modules::aax::build_from_adx_segments(std::move(config), [log](QString message) {
                    push_job_log(log, std::move(message));
                });
            }),
            log
        );
    }

    void open_csb_directory_build_wizard() {
        if (save_running() || m_transform_kind != TransformKind::Csb) {
            return;
        }

        auto selected = modules::csb::choose_directory_build_config(this, m_title);
        if (!selected) {
            QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "CSB folder build"), selected.error());
            return;
        }
        if (!*selected) {
            return;
        }
        auto config = std::move(**selected);

        auto log = std::make_shared<BuildJobLog>();
        auto output_path = config.output_path;
        start_transform_file_job(
            QCoreApplication::translate("Editor.EditorDocumentWidget", "CSB folder build"),
            output_path,
            std::async(std::launch::async, [config = std::move(config), log] () mutable {
                return modules::csb::build_from_directory(std::move(config), [log](QString message) {
                    push_job_log(log, std::move(message));
                });
            }),
            log
        );
    }

    void edit_transform_options() {
        if (save_running()) {
            return;
        }

        auto result = cristudio::edit_transform_options(this, m_transform_kind, transform_view());
        if (!result.handled) {
            return;
        }
        if (!result.log_message.isEmpty()) {
            append_log(result.log_message);
        }
        if (!result.error.isEmpty()) {
            QMessageBox::warning(this, result.warning_title, result.error);
            return;
        }
        if (!result.bytes.empty()) {
            m_bytes = std::move(result.bytes);
            m_dirty = true;
            refresh_summary();
            refresh_title();
        }
    }

    void decode_transform_to_wav() {
        if (save_running() || (m_transform_kind != TransformKind::Adx && m_transform_kind != TransformKind::Hca)) {
            return;
        }
        const auto default_name = safe_output_name(m_title, QStringLiteral(".wav"));
        const auto path_text = QFileDialog::getSaveFileName(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Decode transform to WAV"), default_name);
        if (path_text.isEmpty()) {
            return;
        }
        auto bytes = m_bytes;
        const auto kind = m_transform_kind;
        const auto keys = m_request.keys;
        auto path = path_from_qstring(path_text);
        start_transform_file_job(
            QCoreApplication::translate("Editor.EditorDocumentWidget", "Decode WAV"),
            path,
            std::async(std::launch::async, [kind, bytes = std::move(bytes), path, keys]() mutable {
                return transform_decode_to_wav(kind, std::move(bytes), path, keys);
            })
        );
    }

    void decrypt_transform_to_file() {
        if (save_running() || (m_transform_kind != TransformKind::Adx && m_transform_kind != TransformKind::Hca)) {
            return;
        }
        const auto default_name = safe_output_name(m_title + QStringLiteral("_decrypted"), transform_default_suffix());
        const auto path_text = QFileDialog::getSaveFileName(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Decrypt transform"), default_name);
        if (path_text.isEmpty()) {
            return;
        }
        auto bytes = m_bytes;
        const auto kind = m_transform_kind;
        const auto keys = m_request.keys;
        auto path = path_from_qstring(path_text);
        start_transform_file_job(
            QStringLiteral("Decrypt"),
            path,
            std::async(std::launch::async, [kind, bytes = std::move(bytes), path, keys]() mutable {
                return transform_write_bytes(kind, std::move(bytes), path, keys, QStringLiteral("decrypt"));
            })
        );
    }

    void encrypt_transform_to_file() {
        if (save_running() || m_transform_kind != TransformKind::Hca) {
            return;
        }
        const auto default_name = safe_output_name(m_title + QStringLiteral("_encrypted"), QStringLiteral(".hca"));
        const auto path_text = QFileDialog::getSaveFileName(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Encrypt HCA"), default_name);
        if (path_text.isEmpty()) {
            return;
        }
        auto bytes = m_bytes;
        const auto keys = m_request.keys;
        auto path = path_from_qstring(path_text);
        start_transform_file_job(
            QStringLiteral("Encrypt"),
            path,
            std::async(std::launch::async, [bytes = std::move(bytes), path, keys]() mutable {
                return transform_write_bytes(TransformKind::Hca, std::move(bytes), path, keys, QStringLiteral("encrypt"));
            })
        );
    }

    void rebuild_transform_to_file() {
        if (save_running() || m_transform_kind == TransformKind::None) {
            return;
        }
        const auto default_name = safe_output_name(m_title + QStringLiteral("_rebuilt"), transform_default_suffix());
        const auto path_text = QFileDialog::getSaveFileName(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Rebuild transform"), default_name);
        if (path_text.isEmpty()) {
            return;
        }
        auto bytes = m_bytes;
        const auto kind = m_transform_kind;
        const auto keys = m_request.keys;
        auto path = path_from_qstring(path_text);
        start_transform_file_job(
            QStringLiteral("Rebuild"),
            path,
            std::async(std::launch::async, [kind, bytes = std::move(bytes), path, keys]() mutable {
                return transform_write_bytes(kind, std::move(bytes), path, keys, QStringLiteral("rebuild"));
            })
        );
    }

    void extract_transform_payloads() {
        if (save_running() || (m_transform_kind != TransformKind::Aax && m_transform_kind != TransformKind::Aix &&
                               m_transform_kind != TransformKind::Usm && m_transform_kind != TransformKind::Sfd &&
                               m_transform_kind != TransformKind::Csb && m_transform_kind != TransformKind::Acb)) {
            return;
        }
        const auto dir_text = QFileDialog::getExistingDirectory(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Choose transform extraction folder"));
        if (dir_text.isEmpty()) {
            return;
        }
        auto bytes = m_bytes;
        const auto kind = m_transform_kind;
        auto path = path_from_qstring(dir_text);
        start_transform_file_job(
            QCoreApplication::translate("Editor.EditorDocumentWidget", "Transform extract"),
            path,
            std::async(std::launch::async, [kind, bytes = std::move(bytes), path]() mutable {
                return transform_extract_all(kind, std::move(bytes), path);
            })
        );
    }

    bool build_transform_bytes() {
        auto built = build_transform_session_bytes(m_transform_kind, transform_view());
        if (!built.handled) {
            return false;
        }
        if (!built.log_message.isEmpty()) {
            append_log(built.log_message);
        }
        if (!built.error.isEmpty()) {
            QMessageBox::warning(this, built.warning_title, built.error);
            return true;
        }
        if (!built.bytes.empty()) {
            m_bytes = std::move(built.bytes);
        }
        return true;
    }

    bool build_archive_bytes() {
        if (m_archive_kind == ArchiveKind::Cvm && m_cvm) {
            auto bytes = m_cvm->save(m_cvm_scramble_key);
            if (!bytes) {
                QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "CVM build failed"), utf8_to_qstring(bytes.error()));
                append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "CVM save failed: %1").arg(utf8_to_qstring(bytes.error())));
                return true;
            }
            auto reloaded = cricodecs::cvm::CvmContainer::load(
                std::span<const uint8_t>(bytes->data(), bytes->size()),
                m_cvm_scramble_key);
            if (!reloaded) {
                QMessageBox::warning(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "CVM build failed"),
                    QCoreApplication::translate("Editor.EditorDocumentWidget", "The rebuilt CVM could not be reopened: %1").arg(utf8_to_qstring(reloaded.error())));
                return true;
            }
            m_cvm = std::move(*reloaded);
            m_bytes = std::move(*bytes);
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Built %1 CVM session bytes: %2 bytes")
                .arg(m_cvm_scramble_key.empty() ? QCoreApplication::translate("Editor.EditorDocumentWidget", "unscrambled") : QCoreApplication::translate("Editor.EditorDocumentWidget", "scrambled"))
                .arg(static_cast<qulonglong>(m_bytes.size())));
            return true;
        }
        auto built = build_archive_session_bytes(mutable_archive_view());
        if (!built.handled) {
            return false;
        }
        if (!built.log_message.isEmpty()) {
            append_log(built.log_message);
        }
        if (!built.error.isEmpty()) {
            QMessageBox::warning(this, built.warning_title, built.error);
            return true;
        }
        if (!built.bytes.empty()) {
            m_bytes = std::move(built.bytes);
        }
        return true;
    }

    void build_session_bytes() {
        if (m_utf) {
            m_bytes = modules::utf::build_session_bytes(*m_utf);
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Built UTF session bytes: %1 bytes").arg(static_cast<qulonglong>(m_bytes.size())));
            refresh_summary();
            return;
        }
        if (build_archive_bytes()) {
            refresh_summary();
            return;
        }
        if (build_transform_bytes()) {
            refresh_summary();
            return;
        }
        append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Build is not available for this format yet; Save As will write the independent source bytes."));
    }

    void build_session() {
        if (save_running()) {
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Build skipped because another editor job is running."));
            return;
        }
        m_ui.progress->show();
        append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Build started."));
        build_session_bytes();
        append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Build finished."));
        m_ui.progress->hide();
    }

    QString default_save_name() const {
        auto default_name = m_title;
        if (default_name.contains(QLatin1Char('.'))) {
            return default_name;
        }
        if (m_transform_kind == TransformKind::Acb) {
            default_name += QStringLiteral(".acb");
        } else if (m_utf) {
            default_name += QStringLiteral(".utf");
        } else if (m_archive_kind == ArchiveKind::Afs) {
            default_name += QStringLiteral(".afs");
        } else if (m_archive_kind == ArchiveKind::Awb) {
            default_name += QStringLiteral(".awb");
        } else if (m_archive_kind == ArchiveKind::Acx) {
            default_name += QStringLiteral(".acx");
        } else if (m_archive_kind == ArchiveKind::Cpk) {
            default_name += QStringLiteral(".cpk");
        } else if (m_archive_kind == ArchiveKind::Cvm) {
            default_name += QStringLiteral(".cvm");
        } else if (m_transform_kind != TransformKind::None) {
            default_name += transform_default_suffix();
        } else {
            default_name += QStringLiteral(".bin");
        }
        return default_name;
    }

    void start_save_job(QString label, std::filesystem::path path) {
        m_last_save_path = std::move(path);
        auto bytes = m_bytes;
        m_active_job_log.reset();
        m_active_job_label = label;
        m_active_job_saves_document = true;
        m_save_future = std::async(std::launch::async, [path = m_last_save_path, bytes = std::move(bytes)] {
            return write_file_bytes(path, bytes);
        });
        update_header_actions();
        m_ui.progress->show();
        append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "%1 started: %2").arg(std::move(label)).arg(path_to_qstring(m_last_save_path)));
        m_poll_timer.start(50, this);
    }

    [[nodiscard]] bool save_for_close(QWidget* parent) {
        auto path = m_last_save_file_path;
        if (path.empty()) {
            const auto path_text = QFileDialog::getSaveFileName(parent, QCoreApplication::translate("Editor.EditorDocumentWidget", "Save editor session as"), default_save_name());
            if (path_text.isEmpty()) {
                return false;
            }
            path = path_from_qstring(path_text);
            m_last_save_file_path = path;
        }
        build_session_bytes();
        m_close_after_save = true;
        start_save_job(QCoreApplication::translate("Editor.EditorDocumentWidget", "Save before close"), std::move(path));
        return false;
    }

    void save_as() {
        if (save_running()) {
            return;
        }
        build_session_bytes();
        const auto path_text = QFileDialog::getSaveFileName(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Save editor session as"), default_save_name());
        if (path_text.isEmpty()) {
            return;
        }
        m_last_save_file_path = path_from_qstring(path_text);
        m_close_after_save = false;
        start_save_job(QCoreApplication::translate("Editor.EditorDocumentWidget", "Save As"), m_last_save_file_path);
    }

    void save() {
        if (save_running()) {
            return;
        }
        if (m_last_save_file_path.empty()) {
            save_as();
            return;
        }
        build_session_bytes();
        m_close_after_save = false;
        start_save_job(QStringLiteral("Save"), m_last_save_file_path);
    }

    void close_tab() {
        remove_editor_tab(m_tabs, this);
    }

    void extract_copy() {
        if (save_running()) {
            return;
        }
        build_session_bytes();
        const auto dir_text = QFileDialog::getExistingDirectory(this, QCoreApplication::translate("Editor.EditorDocumentWidget", "Choose extraction folder"));
        if (dir_text.isEmpty()) {
            return;
        }
        if (m_archive_kind != ArchiveKind::None) {
            m_last_save_path = path_from_qstring(dir_text);
            m_active_job_label = QCoreApplication::translate("Editor.EditorDocumentWidget", "Archive extract");
            m_active_job_saves_document = false;
            auto bytes = m_bytes;
            const auto kind = m_archive_kind;
            m_save_future = std::async(std::launch::async, [kind, path = m_last_save_path, bytes = std::move(bytes)]() mutable {
                return extract_archive_bytes(kind, std::move(bytes), path);
            });
            update_header_actions();
            m_ui.progress->show();
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Archive extract started: %1").arg(dir_text));
            m_poll_timer.start(50, this);
            return;
        }
        if (m_transform_kind == TransformKind::Aax || m_transform_kind == TransformKind::Aix ||
            m_transform_kind == TransformKind::Usm || m_transform_kind == TransformKind::Sfd ||
            m_transform_kind == TransformKind::Csb || m_transform_kind == TransformKind::Acb) {
            m_last_save_path = path_from_qstring(dir_text);
            m_active_job_label = QCoreApplication::translate("Editor.EditorDocumentWidget", "Transform extract");
            m_active_job_saves_document = false;
            auto bytes = m_bytes;
            const auto kind = m_transform_kind;
            m_save_future = std::async(std::launch::async, [kind, path = m_last_save_path, bytes = std::move(bytes)]() mutable {
                return transform_extract_all(kind, std::move(bytes), path);
            });
            update_header_actions();
            m_ui.progress->show();
            append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Transform extract started: %1").arg(dir_text));
            m_poll_timer.start(50, this);
            return;
        }
        const auto suffix = m_utf ? QStringLiteral(".utf") : (m_transform_kind != TransformKind::None ? transform_default_suffix() : QStringLiteral(".bin"));
        const auto output_name = safe_output_name(m_title, suffix);
        m_last_save_path = path_from_qstring(dir_text) / path_from_qstring(output_name);
        m_active_job_label = QCoreApplication::translate("Editor.EditorDocumentWidget", "Editor extract");
        m_active_job_saves_document = false;
        auto bytes = m_bytes;
        m_save_future = std::async(std::launch::async, [path = m_last_save_path, bytes = std::move(bytes)] {
            return write_file_bytes(path, bytes);
        });
        update_header_actions();
        m_ui.progress->show();
        append_log(QCoreApplication::translate("Editor.EditorDocumentWidget", "Editor extract started: %1").arg(path_to_qstring(m_last_save_path)));
        m_poll_timer.start(50, this);
    }

    void timerEvent(QTimerEvent* event) override {
        QWidget::timerEvent(event);
        poll_background_work();
        if (!save_running()) {
            m_poll_timer.stop();
        }
    }

    void append_log(const QString& message) {
        m_ui.log->appendPlainText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss  ")) + message);
        m_ui.log->moveCursor(QTextCursor::End);
    }

    void refresh_title() {
        if (m_tabs != nullptr) {
            const auto index = m_tabs->indexOf(this);
            if (index >= 0) {
                m_tabs->setTabText(index, tab_title());
            }
        }
    }

    void update_header_actions() {
        const bool has_session_bytes = m_transform_kind != TransformKind::AudioEncode &&
            m_transform_kind != TransformKind::MediaBuild &&
            !(m_transform_kind == TransformKind::Aax && !m_aax) &&
            !(m_transform_kind == TransformKind::Aix && !m_aix) &&
            !(m_transform_kind == TransformKind::Csb && !m_csb);
        const bool enabled = has_session_bytes && !save_running();
        for (auto* button : {m_ui.save_button, m_ui.save_as_button, m_ui.build_button, m_ui.extract_button}) {
            button->setVisible(has_session_bytes);
            button->setEnabled(enabled);
        }
    }

    EditorOpenRequest m_request;
    QTabWidget* m_tabs = nullptr;
    QString m_title;
    std::optional<LoadedDocument> m_document = std::nullopt;
    std::vector<uint8_t> m_bytes;
    std::optional<cricodecs::utf::UtfTable> m_utf = std::nullopt;
    std::optional<cricodecs::afs::AfsContainer> m_afs = std::nullopt;
    std::optional<cricodecs::awb::AwbContainer> m_awb = std::nullopt;
    std::optional<cricodecs::acx::AcxContainer> m_acx = std::nullopt;
    std::optional<cricodecs::cpk::Cpk> m_cpk = std::nullopt;
    bool m_cpk_obfuscate_utf = false;
    std::optional<cricodecs::cvm::CvmContainer> m_cvm = std::nullopt;
    std::string m_cvm_scramble_key;
    std::optional<cricodecs::adx::Adx> m_adx = std::nullopt;
    std::optional<cricodecs::hca::Hca> m_hca = std::nullopt;
    std::optional<cricodecs::aax::AaxContainer> m_aax = std::nullopt;
    std::optional<cricodecs::aix::Aix> m_aix = std::nullopt;
    std::optional<cricodecs::usm::UsmReader> m_usm = std::nullopt;
    std::optional<cricodecs::sfd::SfdContainer> m_sfd = std::nullopt;
    std::optional<cricodecs::csb::CsbContainer> m_csb = std::nullopt;
    std::optional<cricodecs::acb::AcbContainer> m_acb = std::nullopt;
    ArchiveKind m_archive_kind = ArchiveKind::None;
    TransformKind m_transform_kind = TransformKind::None;
    std::vector<modules::TransformDetailRow> m_transform_rows;
    std::vector<modules::TransformDetailRow> m_usm_chunk_rows;
    std::vector<modules::TransformDetailRow> m_visible_transform_rows;
    mutable std::vector<uint8_t> m_preview_scratch;
    bool m_refreshing_utf = false;
    bool m_refreshing_archive = false;
    bool m_dirty = false;
    bool m_acb_semantic_state_stale = false;
    bool m_close_after_save = false;
    bool m_active_job_saves_document = false;
    QString m_active_job_label;
    std::filesystem::path m_last_save_path;
    std::optional<std::filesystem::path> m_apply_job_output_path = std::nullopt;
    QMediaPlayer* m_editor_media_player = nullptr;
    QAudioOutput* m_editor_audio_output = nullptr;
    QFutureWatcher<EditorAudioPreviewResult>* m_audio_preview_watcher = nullptr;
    std::optional<EditorAudioPreviewRequest> m_pending_audio_preview = std::nullopt;
    uint64_t m_audio_preview_request_id = 0;
    uint64_t m_prepared_media_request_id = 0;
    bool m_editor_slider_dragging = false;
    bool m_editor_resume_after_seek = false;
    bool m_editor_loop_seeking = false;
    uint32_t m_editor_audio_sample_rate = 0;
    std::vector<AudioLoop> m_editor_audio_loops;
    std::filesystem::path m_editor_preview_temp_dir;
    std::filesystem::path m_last_save_file_path;
    std::future<std::expected<void, QString>> m_save_future;
    std::shared_ptr<BuildJobLog> m_active_job_log;
    QBasicTimer m_poll_timer;

    EditorDocumentUi m_ui;
};


} // namespace

QWidget* create_editor_document_widget(EditorOpenRequest request, QTabWidget* tabs) {
    return new EditorDocumentWidget(std::move(request), tabs);
}

bool is_editor_document_widget(QWidget* widget) {
    return dynamic_cast<EditorDocumentWidget*>(widget) != nullptr;
}

QString editor_document_tab_title(QWidget* widget) {
    auto* document = dynamic_cast<EditorDocumentWidget*>(widget);
    return document == nullptr ? QString{} : document->tab_title();
}

bool editor_document_is_dirty(QWidget* widget) {
    auto* document = dynamic_cast<EditorDocumentWidget*>(widget);
    return document != nullptr && document->is_dirty();
}

bool editor_document_has_background_work(QWidget* widget) {
    auto* document = dynamic_cast<EditorDocumentWidget*>(widget);
    return document != nullptr && document->has_background_work();
}

bool editor_document_confirm_close(QWidget* widget, QWidget* parent) {
    auto* document = dynamic_cast<EditorDocumentWidget*>(widget);
    return document == nullptr || document->confirm_close(parent);
}

void retranslate_editor_document(QWidget* widget) {
    if (auto* document = dynamic_cast<EditorDocumentWidget*>(widget); document != nullptr) {
        document->retranslate();
    }
}

} // namespace cristudio
