#pragma once

#include "document/document_types.hpp"
#include "editor/editor_helpers.hpp"
#include "editor/editor_widgets.hpp"
#include "modules/transform_detail.hpp"

#include <cstdint>
#include <cstddef>
#include <span>

class QAction;
class QLabel;
class QComboBox;
class QGridLayout;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTableView;
class QTabWidget;
class QToolButton;
class QTreeView;
class QValidator;
class QWidget;

namespace cricodecs::utf { class UtfTable; }

namespace cristudio {

class EntryTableModel;
class HexPreviewWidget;
class ToggleSwitch;
class TransformDetailModel;
struct ArchiveSessionView;
struct EditorOpenRequest;
struct TransformSessionView;
struct InfoRow;

struct EditorDocumentInfoView {
    const EditorOpenRequest* request = nullptr;
    size_t byte_count = 0;
    const cricodecs::utf::UtfTable* utf = nullptr;
    ArchiveSessionView* archive = nullptr;
    TransformKind transform_kind = TransformKind::None;
    TransformSessionView* transform = nullptr;
};

struct EditorDocumentUi {
    QLabel* title_label = nullptr;
    QLabel* subtitle_label = nullptr;
    QToolButton* save_button = nullptr;
    QToolButton* save_as_button = nullptr;
    QToolButton* build_button = nullptr;
    QToolButton* extract_button = nullptr;
    QWidget* utf_toolbar = nullptr;
    QLabel* table_label = nullptr;
    QLineEdit* table_name_edit = nullptr;
    QPushButton* apply_table_name_button = nullptr;
    QPushButton* add_row_button = nullptr;
    QPushButton* remove_row_button = nullptr;
    QPushButton* add_column_button = nullptr;
    QPushButton* remove_column_button = nullptr;
    QWidget* archive_toolbar = nullptr;
    QLabel* archive_kind_label = nullptr;
    QPushButton* add_archive_file_button = nullptr;
    QPushButton* replace_archive_file_button = nullptr;
    QPushButton* remove_archive_file_button = nullptr;
    QPushButton* move_archive_entry_up_button = nullptr;
    QPushButton* move_archive_entry_down_button = nullptr;
    QPushButton* rename_archive_entry_button = nullptr;
    QPushButton* reserve_afs_id_button = nullptr;
    QPushButton* set_afs_timestamp_button = nullptr;
    QPushButton* set_archive_wave_id_button = nullptr;
    QPushButton* batch_awb_wave_ids_button = nullptr;
    QPushButton* archive_entry_options_button = nullptr;
    QPushButton* archive_options_button = nullptr;
    QPushButton* archive_compression_button = nullptr;
    QAction* archive_compress_all_action = nullptr;
    QAction* archive_store_all_action = nullptr;
    QPushButton* import_afs_als_button = nullptr;
    QPushButton* export_afs_header_button = nullptr;
    QPushButton* import_cvm_script_button = nullptr;
    QPushButton* export_cvm_script_button = nullptr;
    QPushButton* extract_archive_entry_button = nullptr;
    QPushButton* extract_raw_archive_entry_button = nullptr;
    QWidget* transform_toolbar = nullptr;
    QLabel* transform_kind_label = nullptr;
    QPushButton* encode_transform_button = nullptr;
    QPushButton* decode_transform_button = nullptr;
    QPushButton* decrypt_transform_button = nullptr;
    QPushButton* encrypt_transform_button = nullptr;
    QPushButton* rebuild_transform_button = nullptr;
    QPushButton* transform_options_button = nullptr;
    QPushButton* extract_transform_button = nullptr;
    QPushButton* adx_container_build_button = nullptr;
    QPushButton* csb_directory_build_button = nullptr;
    QPushButton* media_build_wizard_button = nullptr;
    QPushButton* editor_mux_preview_button = nullptr;
    QPushButton* open_acb_awb_button = nullptr;
    QPushButton* export_acb_awb_button = nullptr;
    QPushButton* add_transform_entry_button = nullptr;
    QPushButton* replace_transform_entry_button = nullptr;
    QPushButton* remove_transform_entry_button = nullptr;
    QPushButton* move_transform_entry_up_button = nullptr;
    QPushButton* move_transform_entry_down_button = nullptr;
    QPushButton* rename_transform_entry_button = nullptr;
    QPushButton* toggle_transform_entry_flag_button = nullptr;
    QWidget* cri_key_panel = nullptr;
    QLabel* local_key_label = nullptr;
    QComboBox* local_key_type = nullptr;
    QLineEdit* cri_key_edit = nullptr;
    QComboBox* cri_key_base = nullptr;
    QWidget* adx_subkey_panel = nullptr;
    QLabel* adx_subkey_label = nullptr;
    QSpinBox* adx_subkey_spin = nullptr;
    QWidget* adx_triplet_panel = nullptr;
    QLabel* adx_triplet_start_label = nullptr;
    QLabel* adx_triplet_mult_label = nullptr;
    QLabel* adx_triplet_add_label = nullptr;
    QLineEdit* adx_triplet_start = nullptr;
    QLineEdit* adx_triplet_mult = nullptr;
    QLineEdit* adx_triplet_add = nullptr;
    QWidget* cvm_scramble_panel = nullptr;
    ToggleSwitch* cvm_scramble_check = nullptr;
    QLabel* cvm_scramble_label = nullptr;
    QPushButton* apply_cri_key_button = nullptr;
    QLineEdit* transform_filter_edit = nullptr;
    QGridLayout* info_grid = nullptr;
    EntryTableModel* table_model = nullptr;
    QTreeView* table = nullptr;
    QTableWidget* utf_grid = nullptr;
    QTableWidget* archive_table = nullptr;
    TransformDetailModel* transform_model = nullptr;
    QTableView* transform_table = nullptr;
    EntryTableModel* field_model = nullptr;
    QTreeView* field_table = nullptr;
    QTableWidget* schema_table = nullptr;
    QTableWidget* payload_table = nullptr;
    HexPreviewWidget* hex_preview = nullptr;
    QWidget* utf_edit_panel = nullptr;
    QWidget* binary_actions_panel = nullptr;
    QLabel* value_label = nullptr;
    QLabel* value_type_label = nullptr;
    QLineEdit* value_edit = nullptr;
    QValidator* unsigned_value_validator = nullptr;
    QValidator* signed_value_validator = nullptr;
    QValidator* real_value_validator = nullptr;
    QValidator* guid_value_validator = nullptr;
    QPushButton* apply_value_button = nullptr;
    QPushButton* rename_column_button = nullptr;
    QPushButton* replace_binary_button = nullptr;
    QTabWidget* preview_tabs = nullptr;
    QWidget* mux_preview_panel = nullptr;
    VideoDisplay video;
    MediaControls media;
    QToolButton* log_toggle_button = nullptr;
    QPlainTextEdit* log = nullptr;
    QProgressBar* progress = nullptr;
};

[[nodiscard]] EditorDocumentUi build_editor_document_ui(QWidget* parent);
void retranslate_editor_document_ui(EditorDocumentUi& ui);
void refresh_archive_document_ui(
    EditorDocumentUi& ui,
    const ArchiveSessionView& view,
    const DecryptionKeys& keys
);
void refresh_transform_document_ui(
    EditorDocumentUi& ui,
    TransformKind kind,
    const TransformSessionView& view,
    const std::vector<modules::TransformDetailRow>& rows,
    QString filter_text
);
void refresh_document_info_ui(EditorDocumentUi& ui, QWidget* parent, const EditorDocumentInfoView& view);

} // namespace cristudio
