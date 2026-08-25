#pragma once

#include "document_loader.hpp"
#include "editor/editor_widgets.hpp"
#include "entry_table_model.hpp"
#include "file_list_model.hpp"
#include "io_reader.hpp"
#include "shared/adx_key_recovery.hpp"
#include "shared/aac_key_recovery.hpp"
#include "shared/hca_key_recovery.hpp"
#include "shared/usm_key_recovery.hpp"

#include <QByteArray>
#include <QFile>
#include <QMainWindow>
#include <QPixmap>
#include <QSortFilterProxyModel>

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

class QAction;
class QActionGroup;
class QAudioOutput;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialog;
class QEvent;
class QFrame;
class QFormLayout;
template <typename T>
class QFutureWatcher;
class QImage;
class QLabel;
class QGridLayout;
class QLineEdit;
class QListView;
class QMenu;
class QMediaPlayer;
class QObject;
class QPlainTextEdit;
class QPoint;
class QProgressBar;
class QScrollArea;
class QShowEvent;
class QSplitter;
class QTemporaryDir;
class QTabWidget;
class QToolButton;
class QTimer;
class QTreeView;
class QResizeEvent;
class QVideoWidget;
class QWidget;

namespace cristudio {

class EditorWorkspace;
class HexPreviewWidget;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void load_startup_paths(std::vector<std::filesystem::path> paths);
    void select_first_loaded_file();
    bool select_first_entry();
    void open_scratch_utf_editor();
    void open_scratch_afs_editor();
    void open_scratch_awb_editor();
    void open_scratch_acx_editor();
    void open_scratch_cpk_editor();
    void open_audio_encode_editor();
    void open_media_build_editor();
    void open_aax_build_editor();
    void open_aix_build_editor();
    void open_csb_build_editor();
    bool open_first_loaded_file_in_editor();
    bool open_first_entry_in_editor();
    [[nodiscard]] bool has_background_work() const;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    enum class Theme {
        Light,
        Dark
    };

    enum class KeyPanelKind {
        None,
        Cri64,
        Adx
    };

    void build_ui();
    void build_menus();
    void bind_ui_text(
        QObject* object,
        const char* property,
        const char* source,
        const char* context = "MainWindow.Chrome"
    );
    void retranslate_ui();
    void apply_pending_language();
    void sync_language_actions();
    [[nodiscard]] bool load_running() const;
    [[nodiscard]] bool extraction_running() const;
    [[nodiscard]] bool materialization_running() const;
    [[nodiscard]] bool preview_running() const;
    [[nodiscard]] bool key_recovery_running() const;
    void open_files();
    void open_folder();
    void new_utf_editor_document();
    void new_afs_editor_document();
    void new_awb_editor_document();
    void new_acx_editor_document();
    void new_cpk_editor_document();
    void new_audio_encode_document();
    void new_media_build_document();
    void new_sfd_build_document();
    void new_aax_build_document();
    void new_aix_build_document();
    void new_csb_build_document();
    void new_cvm_from_script_document();
    void new_cvm_from_directory_document();
    void add_paths(const QList<QUrl>& urls);
    void open_urls_in_editor(const QList<QUrl>& urls);
    void add_path(const std::filesystem::path& path);
    void add_directory(const std::filesystem::path& path);
    void start_loading_paths(std::vector<std::filesystem::path> paths);
    void start_extraction(std::vector<ExtractionTarget> targets, ExtractionMode mode, std::optional<int> mux_audio_choice = std::nullopt);
    void cancel_extraction();
    void start_hca_key_recovery(std::vector<HcaRecoverySource> sources, QString target_label);
    void start_usm_key_recovery(std::vector<UsmRecoverySource> sources, QString target_label);
    void start_adx_key_recovery(
        std::vector<AdxRecoverySource> sources,
        AdxRecoveryKind kind,
        QString target_label
    );
    void start_aac_key_recovery(std::vector<AacRecoverySource> sources, QString target_label);
    void poll_background_work();
    void consume_load_result();
    void consume_extract_result();
    void consume_hca_key_recovery_result();
    void consume_usm_key_recovery_result();
    void consume_adx_key_recovery_result();
    void consume_aac_key_recovery_result();
    void start_document_materialization(int row);
    void select_document(int row);
    void consume_materialize_result();
    void start_entry_preview(EntrySummary entry);
    void start_entry_preview_now(EntrySummary entry);
    void show_acb_cue(uint32_t cue_index);
    void refresh_acb_cue_route_choices();
    void start_acb_cue_preview();
    void apply_current_entry_view_mode();
    void update_entry_view_mode_labels(bool cue_view);
    void show_entry_inspector(const EntrySummary& entry);
    void consume_preview_result();
    void show_document(const LoadedDocument* document);
    void populate_document_raw_tab(const LoadedDocument& document, bool select_raw);
    void show_preview_document(const LoadedDocument& document);
    void clear_preview_panel();
    void start_document_audio_preview(const LoadedDocument& document);
    void start_document_video_preview(const LoadedDocument& document);
    void start_document_mux_preview(const LoadedDocument& document, int audio_choice = 0);
    void configure_audio_preview(const AudioPreview& audio);
    void configure_video_preview(const VideoPreview& video);
    void configure_mux_preview(const MuxPreview& mux);
    void show_media_preview_message(const QString& message);
    void show_playable_media_controls();
    void release_video_preview_resources();
    void recreate_video_widget();
    void reset_audio_preview();
    void update_audio_time_label();
    void update_loop_controls(const AudioPreview& audio);
    void handle_loop_position(qint64 position);
    void set_preview_image(const QImage& image);
    void update_preview_image();
    void toggle_left_panel();
    void open_preview_panel();
    void toggle_preview_panel();
    void clear_loaded_files();
    void unload_selected_files();
    void open_selected_files_in_editor();
    void open_selected_entries_in_editor();
    void open_selected_nested_entries_in_editor();
    void show_loaded_file_context_menu(const QPoint& position);
    void show_entry_context_menu(const QPoint& position);
    void show_nested_entry_context_menu(const QPoint& position);
    void show_decryption_keys_panel();
    void build_decryption_keys_window();
    void retranslate_decryption_keys_window();
    void sync_decryption_key_panel_from_state();
    void update_decryption_key_derived_fields();
    bool read_decryption_key_panel(DecryptionKeys& next);
    bool apply_decryption_key_panel();
    void refresh_key_profile_combo();
    void save_current_key_profile();
    void load_selected_key_profile();
    void delete_selected_key_profile();
    void refresh_current_preview();
    [[nodiscard]] bool reload_current_document_with_keys();
    void update_document_key_panel(const LoadedDocument* document);
    void update_preview_key_panel(const LoadedDocument* document);
    void update_preview_key_panel(const EntrySummary& entry);
    [[nodiscard]] KeyPanelKind key_kind_for_document(const LoadedDocument& document) const;
    [[nodiscard]] KeyPanelKind key_kind_for_entry(const EntrySummary& entry) const;
    void update_key_panel(
        QWidget* panel,
        QLabel* label,
        QLineEdit* input,
        QComboBox* base,
        QToolButton* apply,
        KeyPanelKind kind
    );
    void apply_key_panel_value(KeyPanelKind kind, const QString& value, int numeric_base = 16);
    void populate_entry_preview_metadata(const EntrySummary& entry);
    void populate_info_grid(QGridLayout* grid, const std::vector<InfoRow>& rows);
    void position_edge_buttons();
    void schedule_position_edge_buttons();
    void set_drop_overlay_visible(bool visible);
    void update_loading_indicator();
    void update_extraction_indicator();
    void update_file_sort();
    void update_file_list_status();
    void update_entry_selection_status();
    void update_memory_usage_label();
    void prepare_document_raw_tab(const LoadedDocument& document);
    void set_entry_list_path(const QString& path);
    void update_entry_path_bar();
    void activate_current_entry();
    void set_preview_entry_actions_visible(bool visible);
    void fit_entry_columns(QTreeView* view, bool custom_columns) const;
    [[nodiscard]] std::optional<int> current_mux_audio_choice() const;
    [[nodiscard]] std::vector<ExtractionTarget> selected_file_targets() const;
    [[nodiscard]] std::vector<ExtractionTarget> all_file_targets() const;
    [[nodiscard]] std::vector<ExtractionTarget> selected_entry_targets() const;
    [[nodiscard]] std::vector<ExtractionTarget> selected_nested_entry_targets() const;
    [[nodiscard]] std::vector<ExtractionTarget> current_preview_entry_targets() const;
    [[nodiscard]] std::optional<ExtractionTarget>
    acb_cue_extraction_target(
        uint32_t cue_index,
        std::optional<uint32_t> route_index = std::nullopt) const;
    [[nodiscard]] std::vector<HcaRecoverySource> selected_file_recovery_sources() const;
    [[nodiscard]] std::vector<HcaRecoverySource> all_file_recovery_sources() const;
    [[nodiscard]] std::vector<HcaRecoverySource> selected_entry_recovery_sources() const;
    [[nodiscard]] std::vector<HcaRecoverySource> selected_nested_entry_recovery_sources() const;
    [[nodiscard]] std::vector<HcaRecoverySource> current_preview_recovery_sources() const;
    [[nodiscard]] std::vector<UsmRecoverySource> selected_file_usm_recovery_sources() const;
    [[nodiscard]] std::vector<UsmRecoverySource> all_file_usm_recovery_sources() const;
    [[nodiscard]] std::vector<UsmRecoverySource> selected_entry_usm_recovery_sources() const;
    [[nodiscard]] std::vector<UsmRecoverySource> selected_nested_entry_usm_recovery_sources() const;
    [[nodiscard]] std::vector<UsmRecoverySource> current_preview_usm_recovery_sources() const;
    [[nodiscard]] std::vector<AdxRecoverySource> selected_file_adx_recovery_sources() const;
    [[nodiscard]] std::vector<AdxRecoverySource> all_file_adx_recovery_sources() const;
    [[nodiscard]] std::vector<AdxRecoverySource> selected_entry_adx_recovery_sources() const;
    [[nodiscard]] std::vector<AdxRecoverySource> selected_nested_entry_adx_recovery_sources() const;
    [[nodiscard]] std::vector<AdxRecoverySource> current_preview_adx_recovery_sources() const;
    [[nodiscard]] std::optional<AdxRecoveryKind> current_preview_adx_recovery_kind() const;
    [[nodiscard]] std::vector<AacRecoverySource> selected_file_aac_recovery_sources() const;
    [[nodiscard]] std::vector<AacRecoverySource> all_file_aac_recovery_sources() const;
    [[nodiscard]] std::vector<AacRecoverySource> selected_entry_aac_recovery_sources() const;
    [[nodiscard]] std::vector<AacRecoverySource> selected_nested_entry_aac_recovery_sources() const;
    [[nodiscard]] std::vector<AacRecoverySource> current_preview_aac_recovery_sources() const;
    void set_theme(Theme theme);
    void set_compact_lists(bool compact);
    void restore_ui_state();
    void save_ui_state() const;
    void append_log(const QString& message);
    [[nodiscard]] QString log_path() const;
    [[nodiscard]] const LoadedDocument* ensure_loaded_document(int row);
    [[nodiscard]] bool ensure_media_backend();
    [[nodiscard]] QString ffmpeg_executable_path();
    [[nodiscard]] bool has_ffmpeg();
    [[nodiscard]] QString ffmpeg_missing_message() const;

    struct LoadResult {
        std::vector<std::pair<LoadedDocument, QString>> loaded;
        std::vector<QString> log_messages;
        size_t candidate_count = 0;
        size_t rejected_count = 0;
    };

    struct LoadProgress {
        std::atomic_size_t candidate_count = 0;
        std::atomic_size_t valid_count = 0;
        std::atomic_size_t rejected_count = 0;
    };

    struct ExtractionProgress {
        std::atomic_size_t target_count = 0;
        std::atomic_size_t processed_count = 0;
        std::atomic_size_t extracted_count = 0;
        std::atomic_size_t failed_count = 0;
    };

    struct PreviewResult {
        std::optional<LoadedDocument> document = std::nullopt;
        std::optional<AudioPreview> audio = std::nullopt;
        std::optional<VideoPreview> video = std::nullopt;
        std::optional<MuxPreview> mux = std::nullopt;
        QString message;
        QByteArray raw_bytes;
        uint64_t raw_total_size = 0;
        QByteArray preview_bytes;
        bool acb_cue_preview = false;
        uint64_t request_id = 0;
    };

    struct MaterializeResult {
        QString canonical_path;
        std::optional<LoadedDocument> document = std::nullopt;
        std::string rejection_reason;
        uint64_t request_id = 0;
    };

    struct HcaRecoveredTarget {
        HcaKeyRecoveryResult recovered;
        QString source;
    };

    struct HcaKeyRecoveryTaskResult {
        std::vector<HcaRecoveredTarget> recovered;
        std::vector<QString> errors;
        QString target_label;
        size_t requested_sources = 0;
        uint64_t request_id = 0;
        bool canceled = false;
    };

    struct UsmKeyRecoveryTaskResult {
        std::optional<UsmKeyRecoveryReport> report = std::nullopt;
        QString error;
        QString target_label;
        size_t requested_sources = 0;
        uint64_t request_id = 0;
    };

    struct AdxRecoveredTarget {
        AdxKeyRecoveryResult recovered;
        QString source;
    };

    struct AdxKeyRecoveryTaskResult {
        std::vector<AdxRecoveredTarget> recovered;
        std::vector<QString> errors;
        QString target_label;
        AdxRecoveryKind kind = AdxRecoveryKind::Adx;
        size_t requested_sources = 0;
        uint64_t request_id = 0;
    };

    struct AacRecoveredTarget {
        AacKeyRecoveryResult recovered;
        QString source;
    };

    struct AacKeyRecoveryTaskResult {
        std::vector<AacRecoveredTarget> recovered;
        std::vector<QString> errors;
        QString target_label;
        size_t requested_sources = 0;
        uint64_t request_id = 0;
    };

    struct UiTextBinding {
        QObject* object = nullptr;
        const char* property = nullptr;
        const char* source = nullptr;
        const char* context = nullptr;
    };

    FileListModel* m_file_model = nullptr;
    QSortFilterProxyModel* m_file_proxy = nullptr;
    EntryTableModel* m_entry_model = nullptr;
    QSortFilterProxyModel* m_entry_proxy = nullptr;

    QLineEdit* m_file_filter = nullptr;
    QComboBox* m_file_sort = nullptr;
    QComboBox* m_file_type_filter = nullptr;
    QLabel* m_file_list_status = nullptr;
    QLineEdit* m_entry_filter = nullptr;
    QWidget* m_entry_filter_row = nullptr;
    QComboBox* m_entry_view_mode = nullptr;
    QWidget* m_entry_path_row = nullptr;
    QToolButton* m_entry_up_button = nullptr;
    QLabel* m_entry_path_label = nullptr;
    QTabWidget* m_workspace_tabs = nullptr;
    EditorWorkspace* m_editor_workspace = nullptr;
    QListView* m_file_view = nullptr;
    QWidget* m_content_host = nullptr;
    QWidget* m_left_edge_rail = nullptr;
    QWidget* m_left_panel = nullptr;
    QLabel* m_doc_title = nullptr;
    QLabel* m_doc_subtitle = nullptr;
    QGridLayout* m_info_grid = nullptr;
    QWidget* m_doc_key_panel = nullptr;
    QLabel* m_doc_key_label = nullptr;
    QLineEdit* m_doc_key_input = nullptr;
    QComboBox* m_doc_key_base_input = nullptr;
    QToolButton* m_doc_key_apply = nullptr;
    QToolButton* m_doc_mux_preview_button = nullptr;
    QToolButton* m_doc_extract_button = nullptr;
    QToolButton* m_doc_extract_raw_button = nullptr;
    QTreeView* m_entry_view = nullptr;
    QLabel* m_entry_selection_status = nullptr;
    QWidget* m_main_bottom_spacer = nullptr;
    QFrame* m_drop_overlay = nullptr;
    QSplitter* m_splitter = nullptr;
    QToolButton* m_left_panel_button = nullptr;
    QToolButton* m_clear_files_button = nullptr;
    QToolButton* m_preview_panel_button = nullptr;
    QWidget* m_right_edge_rail = nullptr;
    QWidget* m_nested_panel = nullptr;
    int m_left_panel_width = 320;
    int m_preview_panel_width = 420;
    EntryTableModel* m_nested_entry_model = nullptr;
    QLabel* m_nested_title = nullptr;
    QLabel* m_nested_subtitle = nullptr;
    QGridLayout* m_nested_info_grid = nullptr;
    QWidget* m_nested_info_panel = nullptr;
    QWidget* m_acb_cue_controls = nullptr;
    QFormLayout* m_acb_cue_selector_form = nullptr;
    QComboBox* m_acb_cue_route_combo = nullptr;
    QCheckBox* m_acb_include_empty_holds = nullptr;
    QWidget* m_preview_key_panel = nullptr;
    QLabel* m_preview_key_label = nullptr;
    QLineEdit* m_preview_key_input = nullptr;
    QComboBox* m_preview_key_base_input = nullptr;
    QToolButton* m_preview_key_apply = nullptr;
    QToolButton* m_preview_extract_button = nullptr;
    QToolButton* m_preview_extract_raw_button = nullptr;
    QToolButton* m_preview_recover_key_button = nullptr;
    QToolButton* m_preview_recover_usm_key_button = nullptr;
    QToolButton* m_preview_recover_adx_key_button = nullptr;
    QToolButton* m_preview_recover_aac_key_button = nullptr;
    QTabWidget* m_preview_tabs = nullptr;
    HexPreviewWidget* m_raw_hex = nullptr;
    QScrollArea* m_nested_image_scroll = nullptr;
    QLabel* m_nested_image = nullptr;
    QTreeView* m_nested_entry_view = nullptr;
    QPlainTextEdit* m_nested_body = nullptr;
    VideoDisplay m_video;
    MediaControls m_media;
    QMediaPlayer* m_audio_player = nullptr;
    QAudioOutput* m_audio_output = nullptr;
    QPixmap m_nested_source_pixmap;
    QProgressBar* m_loading_bar = nullptr;
    QLabel* m_loading_status_label = nullptr;
    QToolButton* m_cancel_extraction_button = nullptr;
    QLabel* m_memory_usage_label = nullptr;
    QAction* m_toggle_left_action = nullptr;
    QAction* m_toggle_preview_action = nullptr;
    QMenu* m_edit_menu = nullptr;
    QAction* m_decryption_keys_action = nullptr;
    QAction* m_extract_mux_outputs_action = nullptr;
    QAction* m_extract_acb_cues_action = nullptr;
    QAction* m_light_theme_action = nullptr;
    QAction* m_dark_theme_action = nullptr;
    QAction* m_always_show_access_keys_action = nullptr;
    QAction* m_compact_lists_action = nullptr;
    QActionGroup* m_language_group = nullptr;
    QString m_pending_language_code;
    std::vector<UiTextBinding> m_ui_text_bindings;
    bool m_retranslate_pending = false;
    Theme m_theme = Theme::Light;
    QDialog* m_decryption_keys_window = nullptr;
    QComboBox* m_key_profile_combo = nullptr;
    QLineEdit* m_cri_key_input = nullptr;
    QComboBox* m_cri_key_base_input = nullptr;
    QLineEdit* m_hca_subkey_input = nullptr;
    QComboBox* m_hca_subkey_base_input = nullptr;
    QComboBox* m_adx_mode_input = nullptr;
    QLineEdit* m_adx_string_input = nullptr;
    QLineEdit* m_adx_number_input = nullptr;
    QComboBox* m_adx_number_base_input = nullptr;
    QLineEdit* m_adx_subkey_input = nullptr;
    QComboBox* m_adx_subkey_base_input = nullptr;
    QLineEdit* m_adx_triplet_start_input = nullptr;
    QLineEdit* m_adx_triplet_mult_input = nullptr;
    QLineEdit* m_adx_triplet_add_input = nullptr;
    QLabel* m_adx_triplet_status = nullptr;

    QFile m_log_file;
    QTimer* m_work_timer = nullptr;
    QTimer* m_memory_usage_timer = nullptr;
    QFutureWatcher<LoadResult>* m_load_watcher = nullptr;
    std::shared_ptr<LoadProgress> m_load_progress;
    bool m_drop_active_load_result = false;
    std::vector<std::filesystem::path> m_queued_load_paths;
    QFutureWatcher<ExtractionReport>* m_extract_watcher = nullptr;
    std::shared_ptr<ExtractionProgress> m_extract_progress;
    std::stop_source m_extract_stop_source;
    QFutureWatcher<HcaKeyRecoveryTaskResult>* m_hca_key_recovery_watcher = nullptr;
    uint64_t m_hca_key_recovery_request_id = 0;
    std::stop_source m_hca_key_recovery_stop_source;
    QFutureWatcher<UsmKeyRecoveryTaskResult>* m_usm_key_recovery_watcher = nullptr;
    uint64_t m_usm_key_recovery_request_id = 0;
    QFutureWatcher<AdxKeyRecoveryTaskResult>* m_adx_key_recovery_watcher = nullptr;
    uint64_t m_adx_key_recovery_request_id = 0;
    QFutureWatcher<AacKeyRecoveryTaskResult>* m_aac_key_recovery_watcher = nullptr;
    uint64_t m_aac_key_recovery_request_id = 0;
    QFutureWatcher<MaterializeResult>* m_materialize_watcher = nullptr;
    uint64_t m_materialize_request_id = 0;
    QString m_materialize_canonical_path;
    QString m_pending_materialize_canonical_path;
    QFutureWatcher<PreviewResult>* m_preview_watcher = nullptr;
    uint64_t m_preview_request_id = 0;
    bool m_audio_slider_dragging = false;
    bool m_audio_resume_after_seek = false;
    bool m_audio_loop_seeking = false;
    uint32_t m_audio_sample_rate = 0;
    std::vector<AudioLoop> m_audio_loops;
    qint64 m_preview_duration_ms = 0;
    QString m_audio_source_path;
    std::filesystem::path m_video_temp_dir;
    std::vector<std::filesystem::path> m_deferred_video_temp_dirs;
    std::unique_ptr<QTemporaryDir> m_audio_temp_dir;
    std::shared_ptr<const modules::acb::CueSheetView> m_acb_cue_sheet;
    std::filesystem::path m_acb_cue_source_path;
    std::optional<uint32_t> m_current_acb_cue_index = std::nullopt;
    std::optional<uint32_t> m_acb_last_cue_index = std::nullopt;
    std::optional<uint32_t> m_acb_last_waveform_index = std::nullopt;
    std::vector<std::pair<std::string, QComboBox*>> m_acb_selector_controls;
    bool m_showing_acb_cues = false;
    bool m_pending_acb_cue_preview = false;
    std::optional<EntrySummary> m_pending_preview_entry = std::nullopt;
    std::optional<std::pair<std::filesystem::path, int>> m_pending_mux_preview = std::nullopt;
    std::optional<EntrySummary> m_current_preview_entry = std::nullopt;
    std::optional<cricodecs::io::reader> m_document_raw_reader = std::nullopt;
    std::optional<std::filesystem::path> m_document_raw_path = std::nullopt;
    bool m_skip_next_file_click_reload = false;
    std::optional<QString> m_ffmpeg_executable;
    DecryptionKeys m_decryption_keys;
    KeyPanelKind m_doc_key_kind = KeyPanelKind::None;
    KeyPanelKind m_preview_key_kind = KeyPanelKind::None;
};

} // namespace cristudio
