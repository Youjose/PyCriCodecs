#include "shared/i18n.hpp"
#include "../main_window.hpp"

#include "../editor/hex_patterns.hpp"
#include "../editor/hex_preview_widget.hpp"
#include "../modules/acb/acb_cue_view.hpp"
#include "../path_text.hpp"
#include "../shared/document_helpers.hpp"
#include "preview_helpers.hpp"
#include "ui_helpers.hpp"

#include <QCoreApplication>
#include <QAudioOutput>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QGridLayout>
#include <QFutureWatcher>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMediaPlayer>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVideoWidget>
#include <QWidget>
#include <QtConcurrentRun>

#include <algorithm>
#include <exception>
#include <array>
#include <filesystem>
#include <system_error>
#include <typeinfo>
#include <utility>

namespace cristudio {

void MainWindow::start_entry_preview(EntrySummary entry) {
    start_entry_preview_now(std::move(entry));
}

void MainWindow::start_entry_preview_now(EntrySummary entry) {
    m_pending_acb_cue_preview = false;
    if (m_acb_cue_controls != nullptr) {
        m_acb_cue_controls->hide();
    }
    m_pending_mux_preview = std::nullopt;
    m_document_raw_reader.reset();
    m_document_raw_path.reset();
    m_current_preview_entry = entry;
    set_preview_entry_actions_visible(entry.has_source);
    open_preview_panel();
    m_nested_title->setText(archive_basename(strip_mux_prefix(utf8_to_qstring(entry.name))));
    m_nested_subtitle->setText(utf8_to_qstring(entry.type.empty() ? entry.source_format : entry.type));
    populate_entry_preview_metadata(entry);
    update_preview_key_panel(entry);
    m_nested_image_scroll->hide();
    m_nested_source_pixmap = {};
    m_nested_entry_model->clear();
    m_nested_entry_view->hide();
    m_nested_body->setMaximumHeight(QWIDGETSIZE_MAX);
    reset_audio_preview();
    if (m_raw_hex != nullptr) {
        m_raw_hex->clear_bytes();
    }
    if (m_preview_tabs != nullptr) {
        m_preview_tabs->setTabEnabled(0, true);
        m_preview_tabs->setTabEnabled(1, false);
        m_preview_tabs->setCurrentIndex(0);
        m_preview_tabs->show();
    }
    show_media_preview_message(QCoreApplication::translate("MainWindow.PreviewPanel", "Loading preview..."));

    if (preview_running()) {
        ++m_preview_request_id;
        m_pending_preview_entry = std::move(entry);
        return;
    }

    m_pending_preview_entry = std::nullopt;
    const auto request_id = ++m_preview_request_id;
    append_log(QCoreApplication::translate("MainWindow.PreviewPanel", "Entry preview started [%1]: %2")
        .arg(request_id)
        .arg(utf8_to_qstring(entry.name)));
    auto keys = m_decryption_keys;
    m_preview_watcher->setFuture(QtConcurrent::run([entry, request_id, keys = std::move(keys)] {
        auto stage = QCoreApplication::translate("MainWindow.PreviewPanel", "extracting and identifying the embedded entry");
        try {
            const auto make_result = [request_id, &stage](EmbeddedPreview preview) {
                PreviewResult result;
                result.request_id = request_id;
                if (preview.document) {
                    result.document = std::move(*preview.document);
                }
                if (preview.audio) {
                    result.audio = std::move(*preview.audio);
                }
                if (preview.video) {
                    stage = QCoreApplication::translate("MainWindow.PreviewPanel", "preparing the embedded video for playback");
                    prepare_video_preview_for_playback(*preview.video);
                    result.video = std::move(*preview.video);
                }
                result.message = QString::fromStdString(preview.message);
                if (!preview.raw_preview_bytes.empty()) {
                    result.raw_bytes = std::move(preview.raw_preview_bytes);
                }
                if (!preview.preview_bytes.empty()) {
                    result.preview_bytes = QByteArray(
                        reinterpret_cast<const char*>(preview.preview_bytes.data()),
                        static_cast<qsizetype>(preview.preview_bytes.size())
                    );
                }
                return result;
            };

            auto result = make_result(load_embedded_entry_preview(entry, keys));
            const auto should_try_without_cri_key =
                keys.has_cri_key &&
                entry.source_format == "USM" &&
                (
                    (result.video && result.video->playable_path.empty()) ||
                    (!result.video && !result.audio && !result.document)
                );
            if (should_try_without_cri_key) {
                auto fallback_keys = keys;
                fallback_keys.has_cri_key = false;
                fallback_keys.cri_key = 0;
                auto fallback_result = make_result(load_embedded_entry_preview(entry, fallback_keys));
                const auto fallback_usable =
                    (fallback_result.video && !fallback_result.video->playable_path.empty()) ||
                    fallback_result.audio ||
                    fallback_result.document;
                if (fallback_usable) {
                    if (result.video) {
                        remove_preview_temporary_directory(*result.video);
                    }
                    result = std::move(fallback_result);
                } else if (fallback_result.video) {
                    remove_preview_temporary_directory(*fallback_result.video);
                }
            }
            return result;
        } catch (const std::exception& error) {
            PreviewResult result;
            result.request_id = request_id;
            result.message = QCoreApplication::translate("MainWindow.PreviewPanel", "Preview failed while %1: %2 [%3]")
                .arg(
                    stage,
                    QString::fromLocal8Bit(error.what()),
                    QString::fromLatin1(typeid(error).name())
                );
            return result;
        } catch (...) {
            PreviewResult result;
            result.request_id = request_id;
            result.message = QCoreApplication::translate("MainWindow.PreviewPanel", "Preview failed while %1 with an unknown exception").arg(stage);
            return result;
        }
    }));
}

void MainWindow::show_entry_inspector(const EntrySummary& entry) {
    m_pending_acb_cue_preview = false;
    if (m_acb_cue_controls != nullptr) {
        m_acb_cue_controls->hide();
    }
    ++m_preview_request_id;
    m_pending_preview_entry = std::nullopt;
    m_current_preview_entry = std::nullopt;
    m_document_raw_reader.reset();
    m_document_raw_path.reset();
    set_preview_entry_actions_visible(false);
    open_preview_panel();

    reset_audio_preview();
    release_video_preview_resources();
    m_nested_source_pixmap = {};
    if (m_nested_image_scroll != nullptr) {
        m_nested_image_scroll->hide();
    }
    if (m_nested_body != nullptr) {
        m_nested_body->clear();
        m_nested_body->hide();
    }
    if (m_raw_hex != nullptr) {
        m_raw_hex->clear_bytes();
    }
    if (m_preview_tabs != nullptr) {
        m_preview_tabs->setTabEnabled(0, true);
        m_preview_tabs->setTabEnabled(1, false);
        m_preview_tabs->setCurrentIndex(0);
        m_preview_tabs->show();
    }
    if (m_preview_key_panel != nullptr) {
        update_preview_key_panel(entry);
    }

    m_nested_title->setText(strip_mux_prefix(utf8_to_qstring(entry.name)));
    m_nested_subtitle->setText(QCoreApplication::translate("MainWindow.PreviewPanel", "UTF row inspector"));
    std::vector<InfoRow> rows;
    rows.push_back(translated_info_row("MainWindow.PreviewPanel", "Row", entry.name));
    rows.push_back(translated_info_row(
        "MainWindow.PreviewPanel",
        "Fields",
        std::to_string(entry.inspector_entries.size())));
    if (!entry.source_path.empty()) {
        rows.push_back(translated_info_row(
            "MainWindow.PreviewPanel",
            "Source archive",
            path_to_utf8(entry.source_path)));
    }
    if (!entry.source_format.empty()) {
        rows.push_back(translated_info_row(
            "MainWindow.PreviewPanel",
            "Source format",
            entry.source_format));
    }
    populate_info_grid(m_nested_info_grid, rows);

    m_nested_entry_model->set_entries(
        entry.inspector_entries,
        {"Field", cristudio::i18n::translate_utf8("MainWindow.PreviewPanel", "Type"), "Value"},
        {"field", "type", "value"}
    );
    m_nested_entry_view->setRootIsDecorated(false);
    m_nested_entry_view->setVisible(!entry.inspector_entries.empty());
    fit_entry_columns(m_nested_entry_view, true);
}

void MainWindow::consume_preview_result() {
    PreviewResult result;
    try {
        result = m_preview_watcher->future().takeResult();
    } catch (const std::exception& error) {
        result.request_id = m_preview_request_id;
        result.message = QCoreApplication::translate("MainWindow.PreviewPanel", "Preview failed: %1 [%2]")
            .arg(QString::fromLocal8Bit(error.what()), QString::fromLatin1(typeid(error).name()));
    } catch (...) {
        result.request_id = m_preview_request_id;
        result.message = QCoreApplication::translate("MainWindow.PreviewPanel", "Preview failed with an unknown exception");
    }

    const auto discard_result_files = [&result] {
        if (result.video) {
            remove_preview_temporary_directory(*result.video);
        }
        if (result.mux) {
            remove_preview_temporary_directory(*result.mux);
        }
    };

    if (m_pending_preview_entry) {
        discard_result_files();
        auto pending = *m_pending_preview_entry;
        m_pending_preview_entry = std::nullopt;
        start_entry_preview(pending);
        return;
    }

    if (m_pending_mux_preview) {
        discard_result_files();
        auto [path, audio_choice] = std::move(*m_pending_mux_preview);
        m_pending_mux_preview = std::nullopt;
        if (m_file_model != nullptr) {
            for (int row = 0; row < m_file_model->rowCount(); ++row) {
                const auto* document = m_file_model->document_at(row);
                if (document != nullptr && document->path == path) {
                    start_document_mux_preview(*document, audio_choice);
                    return;
                }
            }
        }
    }

    if (m_pending_acb_cue_preview && m_showing_acb_cues &&
        m_current_acb_cue_index.has_value()) {
        m_pending_acb_cue_preview = false;
        discard_result_files();
        start_acb_cue_preview();
        return;
    }

    if (result.request_id != m_preview_request_id) {
        discard_result_files();
        return;
    }

    const auto has_raw_preview = !result.raw_bytes.empty();
    const auto preview_succeeded = result.audio.has_value() || result.video.has_value() ||
        (result.mux.has_value() && !result.mux->playable_path.empty()) ||
        result.document.has_value() || has_raw_preview;
    if (!result.message.isEmpty() && !is_low_signal_loader_message(result.message)) {
        append_log(QCoreApplication::translate("MainWindow.PreviewPanel", "Preview result [%1]: %2").arg(result.request_id).arg(result.message));
    } else if (preview_succeeded) {
        append_log(QCoreApplication::translate("MainWindow.PreviewPanel", "Preview completed [%1]").arg(result.request_id));
    }

    if (m_raw_hex != nullptr) {
        if (has_raw_preview) {
            if (m_current_preview_entry.has_value()) {
                m_raw_hex->set_source(std::move(result.raw_bytes), *m_current_preview_entry);
            } else {
                m_raw_hex->set_source(std::move(result.raw_bytes));
            }
        } else if (m_current_preview_entry.has_value()) {
            m_raw_hex->clear_bytes();
        }
    }
    if (m_preview_tabs != nullptr) {
        if (has_raw_preview) {
            m_preview_tabs->setTabEnabled(1, true);
        } else if (m_current_preview_entry.has_value()) {
            m_preview_tabs->setTabEnabled(1, false);
        }
    }
    if (m_preview_tabs != nullptr) {
        m_preview_tabs->show();
        m_preview_tabs->setTabEnabled(0, true);
        m_preview_tabs->setCurrentIndex(0);
    }

    if (result.mux) {
        if (result.document) {
            m_nested_title->setText(archive_basename(utf8_to_qstring(result.document->display_name)));
            m_nested_subtitle->setText(QCoreApplication::translate("MainWindow.PreviewPanel", "Mux preview"));
            populate_info_grid(m_nested_info_grid, result.document->info);
            update_preview_key_panel(&*result.document);
        }
        configure_mux_preview(*result.mux);
        if (!result.message.isEmpty() && result.mux->playable_path.empty()) {
            m_media.status_label->setText(result.message);
        }
    } else if (result.document && is_mux_document(*result.document) && !result.message.isEmpty()) {
        m_nested_title->setText(archive_basename(utf8_to_qstring(result.document->display_name)));
        m_nested_subtitle->setText(QCoreApplication::translate("MainWindow.PreviewPanel", "Mux preview unavailable"));
        populate_info_grid(m_nested_info_grid, result.document->info);
        update_preview_key_panel(&*result.document);
        reset_audio_preview();
        show_media_preview_message(result.message);
    } else if (result.document) {
        show_preview_document(*result.document);
        if (result.audio) {
            configure_audio_preview(*result.audio);
        } else if (result.video) {
            configure_video_preview(*result.video);
        } else if (is_audio_document(*result.document) && !result.message.isEmpty()) {
            fade_widget_in(m_media.panel);
            m_media.play_button->setEnabled(false);
            m_media.seek_slider->setEnabled(false);
            m_media.status_label->setText(result.message);
            m_nested_body->hide();
        }
    } else if (result.video) {
        reset_audio_preview();
        m_nested_subtitle->setText(utf8_to_qstring(result.video->format));
        m_nested_entry_view->hide();
        m_nested_image_scroll->hide();
        m_nested_body->hide();
        configure_video_preview(*result.video);
    } else if (result.audio) {
        if (!result.acb_cue_preview) {
            m_nested_subtitle->setText(utf8_to_qstring(result.audio->format));
            m_nested_entry_view->hide();
        }
        m_nested_image_scroll->hide();
        configure_audio_preview(*result.audio);
    } else if (has_raw_preview) {
        reset_audio_preview();
        if (!result.preview_bytes.isEmpty()) {
            QImage image;
            if (image.loadFromData(result.preview_bytes)) {
                m_nested_subtitle->setText(QCoreApplication::translate("MainWindow.PreviewPanel", "Image preview"));
                set_preview_image(image);
                m_nested_entry_view->hide();
                m_nested_image_scroll->show();
                update_preview_image();
                m_nested_body->hide();
                if (m_preview_tabs != nullptr) {
                    m_preview_tabs->setCurrentIndex(0);
                }
                return;
            }
        }

        m_nested_subtitle->setText(result.message.isEmpty()
            ? QCoreApplication::translate("MainWindow.PreviewPanel", "Unknown format - hex preview")
            : result.message);
        m_nested_entry_view->hide();
        m_nested_image_scroll->hide();
        m_nested_body->hide();
        if (m_preview_tabs != nullptr) {
            m_preview_tabs->setCurrentIndex(1);
        }
    } else {
        reset_audio_preview();
        if (!result.acb_cue_preview) {
            m_nested_subtitle->setText(QCoreApplication::translate("MainWindow.PreviewPanel", "Preview unavailable"));
        }
        show_media_preview_message(
            result.message.isEmpty()
                ? QCoreApplication::translate("MainWindow.PreviewPanel", "Preview unavailable")
                : result.message
        );
        if (result.acb_cue_preview) {
            refresh_acb_cue_route_choices();
        }
    }
}

void MainWindow::show_document(const LoadedDocument* document) {
    if (document == nullptr) {
        ++m_preview_request_id;
        m_pending_preview_entry = std::nullopt;
        m_pending_mux_preview = std::nullopt;
        m_current_preview_entry = std::nullopt;
        m_acb_cue_sheet.reset();
        m_acb_cue_source_path.clear();
        m_current_acb_cue_index = std::nullopt;
        m_showing_acb_cues = false;
        m_pending_acb_cue_preview = false;
        update_entry_view_mode_labels(false);
        set_preview_entry_actions_visible(false);
        clear_preview_panel();
        m_doc_title->setText(QCoreApplication::translate("MainWindow.PreviewPanel", "No file selected"));
        m_doc_subtitle->setText(QCoreApplication::translate("MainWindow.PreviewPanel", "Open or drop CRI files to inspect them."));
        populate_info_grid(m_info_grid, {});
        update_document_key_panel(nullptr);
        if (m_doc_mux_preview_button != nullptr) {
            m_doc_mux_preview_button->hide();
        }
        if (m_doc_extract_button != nullptr) {
            m_doc_extract_button->hide();
        }
        if (m_doc_extract_raw_button != nullptr) {
            m_doc_extract_raw_button->hide();
        }
        m_entry_model->clear();
        if (m_entry_filter_row != nullptr) {
            m_entry_filter_row->hide();
        }
        if (m_entry_path_row != nullptr) {
            m_entry_path_row->hide();
        }
        m_entry_filter->hide();
        m_entry_view->hide();
        if (m_entry_selection_status != nullptr) {
            m_entry_selection_status->hide();
        }
        update_entry_selection_status();
        if (m_main_bottom_spacer != nullptr) {
            m_main_bottom_spacer->show();
        }
        return;
    }

    ++m_preview_request_id;
    m_pending_preview_entry = std::nullopt;
    m_pending_mux_preview = std::nullopt;
    m_current_preview_entry = std::nullopt;
    set_preview_entry_actions_visible(false);
    clear_preview_panel();
    if (m_acb_cue_source_path != document->path) {
        m_acb_last_cue_index = std::nullopt;
        m_acb_last_waveform_index = std::nullopt;
    }
    m_acb_cue_sheet = document->acb_cue_sheet;
    m_acb_cue_source_path = document->path;
    m_current_acb_cue_index = std::nullopt;
    m_showing_acb_cues = false;
    m_pending_acb_cue_preview = false;
    update_entry_view_mode_labels(m_acb_cue_sheet != nullptr);
    m_doc_title->setText(utf8_to_qstring(document->display_name));
    m_doc_subtitle->setText(utf8_to_qstring(localized_document_format(*document)));
    if (!document->summary_loaded) {
        std::vector<InfoRow> rows = document->info;
        rows.push_back(translated_info_row_with_value(
            "MainWindow.PreviewPanel",
            "Details",
            "MainWindow.PreviewPanel",
            "Loading in background"));
        populate_info_grid(m_info_grid, rows);
        update_document_key_panel(document);
        if (m_doc_mux_preview_button != nullptr) {
            m_doc_mux_preview_button->hide();
        }
        if (m_doc_extract_button != nullptr) {
            m_doc_extract_button->hide();
        }
        if (m_doc_extract_raw_button != nullptr) {
            m_doc_extract_raw_button->hide();
        }
        m_entry_model->clear();
        m_entry_view->hide();
        if (m_entry_filter_row != nullptr) {
            m_entry_filter_row->hide();
        }
        if (m_entry_path_row != nullptr) {
            m_entry_path_row->hide();
        }
        if (m_entry_selection_status != nullptr) {
            m_entry_selection_status->hide();
        }
        if (m_main_bottom_spacer != nullptr) {
            m_main_bottom_spacer->show();
        }
        return;
    }

    populate_info_grid(m_info_grid, document->info);
    update_document_key_panel(document);
    if (m_doc_mux_preview_button != nullptr) {
        const auto mux = is_mux_document(*document);
        m_doc_mux_preview_button->setVisible(mux);
        m_doc_mux_preview_button->setEnabled(!mux || has_ffmpeg());
        m_doc_mux_preview_button->setToolTip(mux && !has_ffmpeg()
            ? ffmpeg_missing_message()
            : QCoreApplication::translate("MainWindow.PreviewPanel", "Build playable mux preview"));
    }
    if (m_doc_extract_button != nullptr) {
        m_doc_extract_button->show();
    }
    if (m_doc_extract_raw_button != nullptr) {
        m_doc_extract_raw_button->show();
    }
    const auto has_auto_preview =
        is_mux_document(*document) ||
        is_video_document(*document) ||
        is_audio_document(*document);
    m_document_raw_reader.reset();
    m_document_raw_path.reset();
    if (m_raw_hex != nullptr) {
        m_raw_hex->clear_bytes();
    }
    if (m_preview_tabs != nullptr) {
        m_preview_tabs->setTabEnabled(0, has_auto_preview);
        m_preview_tabs->setTabEnabled(1, true);
        if (!has_auto_preview) {
            prepare_document_raw_tab(*document);
            if (m_preview_tabs->isTabEnabled(1)) {
                m_preview_tabs->setCurrentIndex(1);
            } else {
                m_preview_tabs->setTabEnabled(0, true);
                m_preview_tabs->setCurrentIndex(0);
                show_media_preview_message(QCoreApplication::translate("MainWindow.PreviewPanel", "Raw preview is unavailable for this file."));
            }
        } else {
            m_preview_tabs->setCurrentIndex(0);
        }
    }
    apply_current_entry_view_mode();
    const auto has_entries =
        !document->entries.empty() ||
        (m_acb_cue_sheet != nullptr &&
         !m_acb_cue_sheet->entries.empty());
    if (m_doc_extract_button != nullptr) {
        m_doc_extract_button->setText(has_entries ? QCoreApplication::translate("MainWindow.PreviewPanel", "Extract All") : QCoreApplication::translate("MainWindow.PreviewPanel", "Extract"));
    }
    if (m_doc_extract_raw_button != nullptr) {
        m_doc_extract_raw_button->setText(has_entries ? QCoreApplication::translate("MainWindow.PreviewPanel", "All Raw") : QCoreApplication::translate("MainWindow.PreviewPanel", "Raw"));
    }
    if (m_entry_filter_row != nullptr) {
        m_entry_filter_row->setVisible(has_entries);
    }
    if (!has_entries && m_entry_path_row != nullptr) {
        m_entry_path_row->hide();
    }
    m_entry_filter->setVisible(has_entries);
    m_entry_view->setVisible(has_entries);
    if (m_entry_selection_status != nullptr) {
        m_entry_selection_status->setVisible(has_entries);
    }
    update_entry_selection_status();
    if (m_main_bottom_spacer != nullptr) {
        m_main_bottom_spacer->setVisible(!has_entries);
    }
    if (has_entries) {
        QTimer::singleShot(0, this, [this] {
            if (m_entry_view == nullptr || m_entry_model == nullptr) {
                return;
            }
            fit_entry_columns(m_entry_view, m_entry_model->has_custom_columns());
            if (!m_entry_model->flat_mode() && !m_entry_model->has_custom_columns()) {
                m_entry_view->expandToDepth(6);
            }
        });
    }
    if (is_mux_document(*document)) {
        start_document_mux_preview(*document, 0);
    } else if (is_video_document(*document)) {
        start_document_video_preview(*document);
    } else if (is_audio_document(*document)) {
        start_document_audio_preview(*document);
    }
}

void MainWindow::prepare_document_raw_tab(const LoadedDocument& document) {
    if (m_document_raw_reader.has_value() && m_document_raw_path == document.path) {
        return;
    }
    m_document_raw_reader.emplace();
    if (auto result = m_document_raw_reader->open(document.path, cricodecs::io::access_pattern::random); !result) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.PreviewPanel", "Raw preview failed: could not open file"), 3000);
        m_document_raw_reader.reset();
        m_document_raw_path.reset();
        if (m_raw_hex != nullptr) {
            m_raw_hex->clear_bytes();
        }
        if (m_preview_tabs != nullptr) {
            m_preview_tabs->setTabEnabled(1, false);
        }
        return;
    }
    m_document_raw_path = document.path;

    if (m_raw_hex != nullptr) {
        m_raw_hex->set_source(*m_document_raw_reader, document);
    }
    if (m_preview_tabs != nullptr) {
        m_preview_tabs->setTabEnabled(
            0,
            is_mux_document(document) ||
                is_video_document(document) ||
                is_audio_document(document)
        );
        m_preview_tabs->setTabEnabled(1, true);
    }
}

void MainWindow::populate_document_raw_tab(const LoadedDocument& document, bool select_raw) {
    prepare_document_raw_tab(document);
    if (!m_document_raw_reader.has_value() || !select_raw) {
        return;
    }

    open_preview_panel();
    reset_audio_preview();
    release_video_preview_resources();
    set_preview_entry_actions_visible(false);
    m_current_preview_entry = std::nullopt;
    m_pending_preview_entry = std::nullopt;

    m_nested_title->setText(archive_basename(utf8_to_qstring(document.display_name)));
    m_nested_subtitle->setText(QCoreApplication::translate("MainWindow.PreviewPanel", "Loaded file raw preview"));
    populate_info_grid(m_nested_info_grid, document.info);
    update_preview_key_panel(&document);

    if (m_nested_entry_model != nullptr) {
        m_nested_entry_model->clear();
    }
    if (m_nested_entry_view != nullptr) {
        m_nested_entry_view->hide();
    }
    if (m_nested_image_scroll != nullptr) {
        m_nested_image_scroll->hide();
    }
    if (m_nested_body != nullptr) {
        m_nested_body->hide();
    }
    if (m_preview_tabs != nullptr) {
        m_preview_tabs->show();
        m_preview_tabs->setCurrentIndex(1);
    }
}

void MainWindow::show_preview_document(const LoadedDocument& document) {
    if (m_acb_cue_controls != nullptr) {
        m_acb_cue_controls->hide();
    }
    open_preview_panel();
    m_nested_title->setText(archive_basename(utf8_to_qstring(document.display_name)));
    const auto format = utf8_to_qstring(localized_document_format(document));
    m_nested_subtitle->setText(format);
    populate_info_grid(m_nested_info_grid, document.info);
    update_preview_key_panel(&document);
    reset_audio_preview();
    if (m_preview_tabs != nullptr) {
        m_preview_tabs->show();
        m_preview_tabs->setTabEnabled(0, true);
        m_preview_tabs->setCurrentIndex(0);
    }
    m_nested_source_pixmap = {};
    m_nested_image_scroll->hide();
    m_nested_entry_model->set_entries(document.entries, document.entry_columns, document.entry_column_types);
    m_nested_entry_view->setRootIsDecorated(!m_nested_entry_model->has_custom_columns());
    const auto has_entries = !document.entries.empty();
    m_nested_entry_view->setVisible(has_entries);
    if (has_entries) {
        m_nested_body->hide();
        fit_entry_columns(m_nested_entry_view, m_nested_entry_model->has_custom_columns());
        if (!m_nested_entry_model->has_custom_columns()) {
            m_nested_entry_view->expandToDepth(6);
        }
    } else {
        const auto lower_format = format.toLower();
        const auto is_audio = lower_format.contains(QStringLiteral("adx")) ||
            lower_format.contains(QStringLiteral("ahx")) ||
            lower_format.contains(QStringLiteral("aax")) ||
            lower_format.contains(QStringLiteral("hca")) ||
            lower_format.contains(QStringLiteral("wav"));
        m_nested_body->setVisible(is_audio);
        if (is_audio) {
            m_nested_body->setMaximumHeight(96);
            show_media_preview_message(QCoreApplication::translate("MainWindow.PreviewPanel", "Loading audio preview..."));
        }
    }
}

void MainWindow::clear_preview_panel() {
    reset_audio_preview();
    m_current_preview_entry = std::nullopt;
    m_document_raw_reader.reset();
    m_document_raw_path.reset();
    set_preview_entry_actions_visible(false);
    if (m_acb_cue_controls != nullptr) {
        m_acb_cue_controls->hide();
    }
    if (m_nested_title != nullptr) {
        m_nested_title->setText(QCoreApplication::translate("MainWindow.PreviewPanel", "Entry preview"));
        m_nested_title->setVisible(m_preview_panel_button != nullptr && m_preview_panel_button->isChecked());
    }
    if (m_nested_subtitle != nullptr) {
        m_nested_subtitle->setText(QCoreApplication::translate("MainWindow.PreviewPanel", "Select a supported embedded file."));
        m_nested_subtitle->setVisible(m_preview_panel_button != nullptr && m_preview_panel_button->isChecked());
    }
    if (m_nested_info_grid != nullptr) {
        populate_info_grid(m_nested_info_grid, {});
    }
    update_preview_key_panel(static_cast<const LoadedDocument*>(nullptr));
    if (m_nested_info_panel != nullptr) {
        m_nested_info_panel->setVisible(m_preview_panel_button != nullptr && m_preview_panel_button->isChecked());
    }
    if (m_preview_tabs != nullptr) {
        m_preview_tabs->setTabEnabled(0, true);
        m_preview_tabs->setCurrentIndex(0);
        m_preview_tabs->setVisible(m_preview_panel_button != nullptr && m_preview_panel_button->isChecked());
        m_preview_tabs->setTabEnabled(1, false);
    }
    if (m_raw_hex != nullptr) {
        m_raw_hex->clear_bytes();
    }
    if (m_nested_image != nullptr) {
        m_nested_image->clear();
    }
    m_nested_source_pixmap = {};
    if (m_nested_image_scroll != nullptr) {
        m_nested_image_scroll->hide();
    }
    if (m_nested_entry_model != nullptr) {
        m_nested_entry_model->clear();
    }
    if (m_nested_entry_view != nullptr) {
        m_nested_entry_view->hide();
    }
    if (m_nested_body != nullptr) {
        m_nested_body->setMaximumHeight(QWIDGETSIZE_MAX);
        m_nested_body->clear();
        m_nested_body->hide();
    }
}

void MainWindow::show_media_preview_message(const QString& message) {
    m_video.widget->hide();
    m_video.frame->hide();
    m_media.audio_row->hide();
    m_media.subtitle_row->hide();
    m_media.loop_row->hide();
    m_media.play_button->show();
    m_media.play_button->setEnabled(false);
    m_media.play_button->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_media.play_button->setText(QCoreApplication::translate("MainWindow.PreviewPanel", "Play"));
    m_media.seek_slider->show();
    m_media.seek_slider->setRange(0, 0);
    m_media.seek_slider->setValue(0);
    m_media.seek_slider->setEnabled(false);
    m_media.time_label->show();
    m_media.time_label->setText(QStringLiteral("0:00 / 0:00"));
    m_media.volume_label->hide();
    m_media.volume_slider->hide();
    m_media.status_label->setText(message);
    fade_widget_in(m_media.panel);
    m_nested_entry_view->hide();
    m_nested_image_scroll->hide();
    m_nested_body->hide();
    m_preview_tabs->setCurrentIndex(0);
}

void MainWindow::show_playable_media_controls() {
    m_media.play_button->setEnabled(true);
    m_media.seek_slider->setEnabled(true);
    m_media.volume_label->show();
    m_media.volume_slider->show();
    m_media.volume_slider->setEnabled(true);
    update_audio_time_label();
    fade_widget_in(m_media.panel);
    m_preview_tabs->setCurrentIndex(0);
}

void MainWindow::toggle_preview_panel() {
    if (m_nested_panel == nullptr || m_toggle_preview_action == nullptr) {
        return;
    }
    const auto expanded = m_toggle_preview_action->isChecked();
    if (expanded) {
        m_nested_panel->setMaximumWidth(QWIDGETSIZE_MAX);
        m_nested_panel->setMinimumWidth(0);
        m_nested_panel->show();
        set_preview_entry_actions_visible(
            (m_current_preview_entry.has_value() &&
             m_current_preview_entry->has_source) ||
            (m_showing_acb_cues &&
             m_current_acb_cue_index.has_value())
        );
        if (m_splitter != nullptr) {
            const auto sizes = m_splitter->sizes();
            if (sizes.size() >= 3 && sizes[2] < 360) {
                const auto preview_width = std::max(360, m_preview_panel_width);
                m_splitter->setSizes({
                    sizes.value(0, 320),
                    std::max(0, sizes.value(1, 900) - preview_width),
                    preview_width
                });
            }
        }
    } else {
        if (m_splitter != nullptr) {
            const auto sizes = m_splitter->sizes();
            if (sizes.size() >= 3) {
                if (sizes[2] >= 320) {
                    m_preview_panel_width = sizes[2];
                }
                m_splitter->setSizes({sizes[0], sizes[1] + sizes[2], 0});
            }
        }
        m_nested_panel->setMinimumWidth(0);
        m_nested_panel->setMaximumWidth(0);
        m_nested_panel->hide();
    }
    schedule_position_edge_buttons();
}

void MainWindow::open_preview_panel() {
    if (m_toggle_preview_action != nullptr) {
        m_toggle_preview_action->setChecked(true);
    }
    if (m_preview_panel_button != nullptr) {
        m_preview_panel_button->setChecked(true);
    }
    toggle_preview_panel();
}

void MainWindow::refresh_current_preview() {
    if (m_showing_acb_cues && m_current_acb_cue_index.has_value()) {
        reset_audio_preview();
        start_acb_cue_preview();
        return;
    }

    // Replay the exact nested/archive entry whose preview is visible. Parent and
    // nested views can both retain a current row, so consulting either view
    // first may switch the preview when a key is applied.
    if (m_current_preview_entry.has_value() && m_current_preview_entry->has_source) {
        start_entry_preview(*m_current_preview_entry);
        return;
    }

    if (m_entry_view != nullptr && m_entry_view->currentIndex().isValid()) {
        const auto source = m_entry_proxy->mapToSource(m_entry_view->currentIndex());
        if (const auto* summary = m_entry_model->summary_at(source); summary != nullptr) {
            if (summary->has_source) {
                start_entry_preview(*summary);
                return;
            }
            if (!summary->inspector_entries.empty()) {
                show_entry_inspector(*summary);
                return;
            }
        }
    }

    if (m_nested_entry_view != nullptr && m_nested_entry_view->currentIndex().isValid()) {
        if (const auto* summary = m_nested_entry_model->summary_at(m_nested_entry_view->currentIndex());
            summary != nullptr) {
            if (summary->has_source) {
                start_entry_preview(*summary);
                return;
            }
            if (!summary->inspector_entries.empty()) {
                show_entry_inspector(*summary);
                return;
            }
        }
    }

    if (m_file_view != nullptr && m_file_view->currentIndex().isValid()) {
        const auto source = m_file_proxy->mapToSource(m_file_view->currentIndex());
        show_document(ensure_loaded_document(source.row()));
    }
}

void MainWindow::populate_entry_preview_metadata(const EntrySummary& entry) {
    std::vector<InfoRow> rows;
    rows.push_back(translated_info_row(
        "MainWindow.PreviewPanel",
        "Archive entry",
        strip_mux_prefix(utf8_to_qstring(entry.name)).toUtf8().toStdString()));
    if (entry.type.empty()) {
        rows.push_back(translated_info_row_with_value(
            "MainWindow.PreviewPanel",
            "Type",
            "MainWindow.PreviewPanel",
            "unknown"));
    } else {
        rows.push_back(translated_info_row("MainWindow.PreviewPanel", "Type", entry.type));
    }
    if (!entry.size.empty()) {
        rows.push_back(translated_info_row("MainWindow.PreviewPanel", "Size", entry.size));
    }
    if (!entry.offset.empty()) {
        rows.push_back(translated_info_row("MainWindow.PreviewPanel", "Offset", entry.offset));
    }
    if (!entry.detail.empty()) {
        rows.push_back(translated_info_row("MainWindow.PreviewPanel", "Detail", entry.detail));
    }
    if (entry.has_source) {
        rows.push_back(translated_info_row(
            "MainWindow.PreviewPanel",
            "Source archive",
            path_to_utf8(entry.source_path)));
        rows.push_back(translated_info_row(
            "MainWindow.PreviewPanel",
            "Source format",
            entry.source_format));
        if (entry.has_nested_source) {
            rows.push_back(translated_info_row(
                "MainWindow.PreviewPanel",
                "Nested source",
                entry.nested_source_format));
        }
    }
    populate_info_grid(m_nested_info_grid, rows);
}

void MainWindow::populate_info_grid(QGridLayout* grid, const std::vector<InfoRow>& rows) {
    while (auto* item = grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    if (rows.empty()) {
        grid->addWidget(dim_label(QCoreApplication::translate("MainWindow.PreviewPanel", "No metadata")), 0, 0);
        return;
    }

    int row = 0;
    int slot = 0;
    for (const auto& info : rows) {
        const auto label = utf8_to_qstring(translated_info_name(info));
        const auto value = utf8_to_qstring(translated_info_value(info));
        const auto field_id = info_row_id(info);
        const auto full_width = field_id == "Path" || field_id == "Source archive" || field_id == "Archive entry" || value.size() > 70;
        if (full_width) {
            if (slot != 0) {
                ++row;
                slot = 0;
            }
            grid->addWidget(dim_label(label), row, 0);
            grid->addWidget(value_label(value), row, 1, 1, 3);
            ++row;
            continue;
        }

        const auto column = slot * 2;
        grid->addWidget(dim_label(label), row, column);
        grid->addWidget(value_label(value), row, column + 1);
        ++slot;
        if (slot == 2) {
            slot = 0;
            ++row;
        }
    }
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 0);
    grid->setColumnStretch(3, 1);
}



} // namespace cristudio
