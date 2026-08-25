#include "shared/i18n.hpp"
#include "main_window.hpp"

#include "main_window/ui_helpers.hpp"
#include "path_text.hpp"

#include <QCoreApplication>
#include <QApplication>
#include <QAction>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QProgressBar>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QtConcurrentRun>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace cristudio {
namespace {

bool extraction_message_is_failure_local(const std::string& message) {
    auto lower = QString::fromStdString(message).toLower();
    return lower.contains(QStringLiteral("failed")) ||
           lower.contains(QStringLiteral("error")) ||
           lower.contains(QCoreApplication::translate("MainWindow.Background", "could not")) ||
           lower.contains(QStringLiteral("missing")) ||
           lower.contains(QStringLiteral("needs "));
}

QString extraction_target_label_local(const ExtractionTarget& target) {
    switch (target.kind) {
    case ExtractionTarget::Kind::Document:
        return archive_basename(utf8_to_qstring(target.document.display_name.empty()
            ? target.document.path.filename().generic_string()
            : target.document.display_name));
    case ExtractionTarget::Kind::Entry:
        return archive_basename(strip_mux_prefix(utf8_to_qstring(target.entry.name.empty()
            ? target.entry.type
            : target.entry.name)));
    case ExtractionTarget::Kind::AcbCue:
        return archive_basename(
            utf8_to_qstring(target.acb_output_name.empty()
                ? std::string("ACB cue")
                : target.acb_output_name));
    }
    return QStringLiteral("(unknown)");
}

QString extraction_plan_text_local(
    const std::vector<ExtractionTarget>& targets,
    ExtractionMode mode,
    const QString& output_dir,
    bool include_mux_outputs,
    bool render_acb_cues
) {
    size_t document_count = 0;
    size_t entry_count = 0;
    size_t archive_entry_count = 0;
    for (const auto& target : targets) {
        if (target.kind == ExtractionTarget::Kind::Document) {
            ++document_count;
            archive_entry_count += target.document.entries.size();
        } else {
            ++entry_count;
        }
    }

    QStringList lines;
    lines << QCoreApplication::translate("MainWindow.Background", "Output: %1").arg(output_dir);
    lines << QCoreApplication::translate("MainWindow.Background", "Mode: %1").arg(mode == ExtractionMode::Raw ? QStringLiteral("raw") : QStringLiteral("decoded"));
    lines << QCoreApplication::translate("MainWindow.Background", "Targets: %1 documents, %2 entries").arg(document_count).arg(entry_count);
    if (archive_entry_count != 0) {
        lines << QCoreApplication::translate("MainWindow.Background", "Archive entries queued by selected documents: %1").arg(archive_entry_count);
    }
    lines << QCoreApplication::translate("MainWindow.Background", "USM/SFD mux outputs: %1").arg(include_mux_outputs ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    lines << QCoreApplication::translate("MainWindow.Background", "ACB cue renders: %1").arg(render_acb_cues ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    lines << QString{};
    lines << QCoreApplication::translate("MainWindow.Background", "First targets:");
    const auto shown = std::min<size_t>(targets.size(), 12);
    for (size_t i = 0; i < shown; ++i) {
        lines << QStringLiteral("  %1. %2").arg(i + 1).arg(extraction_target_label_local(targets[i]));
    }
    if (targets.size() > shown) {
        lines << QCoreApplication::translate("MainWindow.Background", "  ... %1 more").arg(targets.size() - shown);
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

void MainWindow::add_paths(const QList<QUrl>& urls) {
    std::vector<std::filesystem::path> paths;
    paths.reserve(urls.size());
    for (const auto& url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        paths.emplace_back(path_from_qstring(url.toLocalFile()));
    }
    start_loading_paths(std::move(paths));
}

void MainWindow::add_path(const std::filesystem::path& path) {
    start_loading_paths({path});
}

void MainWindow::add_directory(const std::filesystem::path& path) {
    start_loading_paths({path});
}

void MainWindow::start_loading_paths(std::vector<std::filesystem::path> paths) {
    if (paths.empty()) {
        return;
    }
    if (load_running()) {
        m_queued_load_paths.insert(
            m_queued_load_paths.end(),
            std::make_move_iterator(paths.begin()),
            std::make_move_iterator(paths.end())
        );
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.Background", "Queued files for loading"), 3000);
        return;
    }

    auto progress = std::make_shared<LoadProgress>();
    m_load_progress = progress;
    m_loading_status_label->setText(QCoreApplication::translate("MainWindow.Background", "0 checked · 0 valid · 0 rejected"));
    m_loading_status_label->show();
    m_loading_bar->show();
    statusBar()->showMessage(QCoreApplication::translate("MainWindow.Background", "Loading assets..."));
    auto keys = m_decryption_keys;
    m_load_watcher->setFuture(QtConcurrent::run([paths = std::move(paths), progress, keys = std::move(keys)]() mutable {
        LoadResult result;

        auto process_file = [&result, progress, &keys](const std::filesystem::path& file_path) {
            ++result.candidate_count;
            progress->candidate_count.fetch_add(1, std::memory_order_relaxed);
            std::error_code canonical_error;
            const auto canonical = std::filesystem::weakly_canonical(file_path, canonical_error);
            if (canonical_error) {
                ++result.rejected_count;
                progress->rejected_count.fetch_add(1, std::memory_order_relaxed);
                result.log_messages.push_back(QCoreApplication::translate("MainWindow.Background", "Rejected path: ") + path_to_qstring(file_path));
                return;
            }

            std::string reason;
            auto document = probe_document_summary(canonical, reason);
            if (!document) {
                ++result.rejected_count;
                progress->rejected_count.fetch_add(1, std::memory_order_relaxed);
                result.log_messages.push_back(
                    QCoreApplication::translate("MainWindow.Background", "Discarded invalid file: ") + path_to_qstring(file_path) +
                    QStringLiteral(" (") + utf8_to_qstring(reason) + QStringLiteral(")")
                );
                return;
            }
            progress->valid_count.fetch_add(1, std::memory_order_relaxed);
            result.loaded.emplace_back(std::move(*document), path_to_qstring(canonical));
        };

        for (const auto& path : paths) {
            std::error_code ec;
            if (std::filesystem::is_directory(path, ec)) {
                for (std::filesystem::recursive_directory_iterator it(
                         path,
                         std::filesystem::directory_options::skip_permission_denied,
                         ec
                     ), end;
                     it != end;
                     it.increment(ec)) {
                    if (ec) {
                        result.log_messages.push_back(
                            QCoreApplication::translate("MainWindow.Background", "Skipped directory entry under ") + path_to_qstring(path) +
                            QStringLiteral(": ") + QString::fromStdString(ec.message())
                        );
                        ec.clear();
                        continue;
                    }
                    if (it->is_regular_file(ec)) {
                        process_file(it->path());
                    }
                    ec.clear();
                }
            } else if (std::filesystem::is_regular_file(path, ec)) {
                process_file(path);
            } else {
                result.log_messages.push_back(QCoreApplication::translate("MainWindow.Background", "Rejected path: ") + path_to_qstring(path));
            }
        }

        return result;
    }));
    m_work_timer->start(50);
}

void MainWindow::start_extraction(std::vector<ExtractionTarget> targets, ExtractionMode mode, std::optional<int> mux_audio_choice) {
    if (targets.empty()) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.Background", "Nothing selected to extract"), 3000);
        return;
    }
    if (extraction_running()) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.Background", "Extraction is already running"), 3000);
        return;
    }

    for (auto& target : targets) {
        if (target.kind != ExtractionTarget::Kind::Document || target.document.summary_loaded) {
            continue;
        }
        std::string reason;
        auto loaded = materialize_document_summary(target.document, reason, m_decryption_keys);
        if (!loaded) {
            statusBar()->showMessage(
                QCoreApplication::translate("MainWindow.Background", "Extraction blocked: could not load document details for %1")
                    .arg(utf8_to_qstring(target.document.display_name)),
                5000
            );
            append_log(
                QCoreApplication::translate("MainWindow.Background", "Extraction blocked: ") +
                utf8_to_qstring(target.document.display_name) +
                QStringLiteral(" (") + utf8_to_qstring(reason) + QStringLiteral(")")
            );
            return;
        }
        target.document = std::move(*loaded);
    }

    const auto output_dir_text = QFileDialog::getExistingDirectory(this, QCoreApplication::translate("MainWindow.Background", "Choose extraction folder"));
    if (output_dir_text.isEmpty()) {
        return;
    }
    const bool include_mux_outputs = m_extract_mux_outputs_action != nullptr &&
        m_extract_mux_outputs_action->isChecked();
    const bool render_acb_cues = m_extract_acb_cues_action != nullptr &&
        m_extract_acb_cues_action->isChecked();
    const auto plan = extraction_plan_text_local(
        targets,
        mode,
        output_dir_text,
        include_mux_outputs,
        render_acb_cues);
    const auto answer = QMessageBox::question(
        this,
        mode == ExtractionMode::Raw ? QCoreApplication::translate("MainWindow.Background", "Raw Extraction Plan") : QCoreApplication::translate("MainWindow.Background", "Extraction Plan"),
        plan,
        QMessageBox::Ok | QMessageBox::Cancel,
        QMessageBox::Ok
    );
    if (answer != QMessageBox::Ok) {
        return;
    }

    auto progress = std::make_shared<ExtractionProgress>();
    progress->target_count.store(targets.size(), std::memory_order_relaxed);
    m_extract_progress = progress;
    m_extract_stop_source = std::stop_source{};
    m_loading_status_label->setText(QCoreApplication::translate("MainWindow.Background", "0/%1 targets · 0 extracted · 0 failed").arg(targets.size()));
    m_loading_status_label->show();
    m_loading_bar->show();
    if (m_cancel_extraction_button != nullptr) {
        m_cancel_extraction_button->setText(QCoreApplication::translate("MainWindow.Background", "Cancel"));
        m_cancel_extraction_button->setEnabled(true);
        m_cancel_extraction_button->show();
    }
    statusBar()->showMessage(mode == ExtractionMode::Raw ? QCoreApplication::translate("MainWindow.Background", "Raw extraction running...") : QCoreApplication::translate("MainWindow.Background", "Extraction running..."));
    const auto keys = m_decryption_keys;
    ExtractionOptions options;
    options.include_mux_outputs = include_mux_outputs;
    options.render_acb_cues = render_acb_cues;
    options.mux_audio_choice = mux_audio_choice.value_or(0);
    options.stop_token = m_extract_stop_source.get_token();
    if (options.include_mux_outputs) {
        options.ffmpeg_path = path_from_qstring(ffmpeg_executable_path());
    }
    const auto output_dir = path_from_qstring(output_dir_text);
    const auto live_log_path = log_path();
    append_log(
        (mode == ExtractionMode::Raw ? QCoreApplication::translate("MainWindow.Background", "Raw extraction started: ") : QCoreApplication::translate("MainWindow.Background", "Extraction started: ")) +
        output_dir_text +
        QCoreApplication::translate("MainWindow.Background", " (%1 selected targets)").arg(targets.size())
    );
    m_extract_watcher->setFuture(QtConcurrent::run(
        [targets = std::move(targets), output_dir, mode, keys, options, progress, live_log_path]() mutable {
            QFile live_log(live_log_path);
            const bool live_log_open = live_log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
            auto write_live_log = [&live_log, live_log_open](const QString& message) {
                if (!live_log_open) {
                    return;
                }
                QTextStream stream(&live_log);
                stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << " " << message << "\n";
                stream.flush();
                live_log.flush();
            };

            auto worker_options = options;
            worker_options.event_callback = [progress, &write_live_log](const ExtractionEvent& event) {
                if (event.processed_delta != 0) {
                    progress->processed_count.fetch_add(event.processed_delta, std::memory_order_relaxed);
                }
                if (event.extracted_delta != 0) {
                    progress->extracted_count.fetch_add(event.extracted_delta, std::memory_order_relaxed);
                }
                if (event.failed_delta != 0) {
                    progress->failed_count.fetch_add(event.failed_delta, std::memory_order_relaxed);
                }
                if (!event.message.empty()) {
                    write_live_log(utf8_to_qstring(event.message));
                }
            };

            auto combined = extract_targets(targets, output_dir, mode, keys, worker_options);
            combined.messages_logged_live = live_log_open;
            if (!combined.canceled) {
                progress->processed_count.store(targets.size(), std::memory_order_relaxed);
            }
            const auto report_name = QStringLiteral("cristudio_extraction_report_%1.txt")
                .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmssZ")));
            const auto report_path = output_dir / qstring_to_utf8(report_name);
            QFile report_file(path_to_qstring(report_path));
            if (report_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream stream(&report_file);
                stream << QCoreApplication::translate("MainWindow.Background", "CriStudio extraction report\n");
                stream << "Time: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n";
                stream << "Mode: " << (mode == ExtractionMode::Raw ? "raw" : "decoded") << "\n";
                stream << "Output: " << path_to_qstring(output_dir) << "\n";
                stream << "Status: " << (combined.canceled ? "canceled" : "completed") << "\n";
                stream << "Summary: " << combined.extracted << " extracted, " << combined.failed << " failed, " << combined.total << " total\n\n";

                stream << QCoreApplication::translate("MainWindow.Background", "Failures and warnings\n");
                bool wrote_failure = false;
                for (const auto& message : combined.messages) {
                    if (!extraction_message_is_failure_local(message)) {
                        continue;
                    }
                    stream << "- " << utf8_to_qstring(message) << "\n";
                    wrote_failure = true;
                }
                if (!wrote_failure) {
                    stream << QCoreApplication::translate("MainWindow.Background", "- none\n");
                }

                stream << "\nOutputs\n";
                if (combined.output_paths.empty()) {
                    stream << QCoreApplication::translate("MainWindow.Background", "- none\n");
                } else {
                    for (const auto& path : combined.output_paths) {
                        stream << "- " << path_to_qstring(path) << "\n";
                    }
                }

                stream << QCoreApplication::translate("MainWindow.Background", "\nFull log\n");
                if (combined.messages.empty()) {
                    stream << QCoreApplication::translate("MainWindow.Background", "- none\n");
                } else {
                    for (const auto& message : combined.messages) {
                        stream << "- " << utf8_to_qstring(message) << "\n";
                    }
                }
                stream.flush();
                report_file.flush();
                combined.diagnostic_path = report_path;
            } else {
                combined.messages.push_back(cristudio::i18n::translate_utf8("MainWindow.Background", "Could not write extraction report: ") + qstring_to_utf8(report_file.errorString()));
            }
            write_live_log(
                QCoreApplication::translate("MainWindow.Background", "Extraction %1: %2 extracted, %3 failed, %4 total")
                    .arg(combined.canceled ? QStringLiteral("canceled") : QStringLiteral("finished"))
                    .arg(combined.extracted)
                    .arg(combined.failed)
                    .arg(combined.total)
            );
            if (combined.diagnostic_path.has_value()) {
                write_live_log(QCoreApplication::translate("MainWindow.Background", "Extraction report: ") + path_to_qstring(*combined.diagnostic_path));
            }
            return combined;
        }
    ));
    m_work_timer->start(50);
}

void MainWindow::cancel_extraction() {
    if (!extraction_running() || m_extract_stop_source.stop_requested()) {
        return;
    }
    m_extract_stop_source.request_stop();
    if (m_cancel_extraction_button != nullptr) {
        m_cancel_extraction_button->setText(QCoreApplication::translate("MainWindow.Background", "Canceling..."));
        m_cancel_extraction_button->setEnabled(false);
    }
    statusBar()->showMessage(QCoreApplication::translate("MainWindow.Background", "Canceling extraction..."));
    append_log(QCoreApplication::translate("MainWindow.Background", "Extraction cancellation requested"));
    update_extraction_indicator();
}

void MainWindow::poll_background_work() {
    if (load_running()) {
        update_loading_indicator();
    }
    if (extraction_running()) {
        update_extraction_indicator();
    }
    if (!load_running() && !extraction_running()) {
        m_work_timer->stop();
    }
}

void MainWindow::update_loading_indicator() {
    if (m_loading_status_label == nullptr || !m_load_progress) {
        return;
    }

    const auto checked = m_load_progress->candidate_count.load(std::memory_order_relaxed);
    const auto valid = m_load_progress->valid_count.load(std::memory_order_relaxed);
    const auto rejected = m_load_progress->rejected_count.load(std::memory_order_relaxed);
    m_loading_status_label->setText(
        QCoreApplication::translate("MainWindow.Background", "%1 checked · %2 valid · %3 rejected").arg(checked).arg(valid).arg(rejected)
    );
}

void MainWindow::update_extraction_indicator() {
    if (m_loading_status_label == nullptr || !m_extract_progress) {
        return;
    }

    const auto total = m_extract_progress->target_count.load(std::memory_order_relaxed);
    const auto processed = m_extract_progress->processed_count.load(std::memory_order_relaxed);
    const auto extracted = m_extract_progress->extracted_count.load(std::memory_order_relaxed);
    const auto failed = m_extract_progress->failed_count.load(std::memory_order_relaxed);
    const auto state = m_extract_stop_source.stop_requested()
        ? QCoreApplication::translate("MainWindow.Background", " · canceling")
        : QString{};
    m_loading_status_label->setText(
        QCoreApplication::translate("MainWindow.Background", "%1/%2 targets · %3 extracted · %4 failed%5")
            .arg(processed)
            .arg(total)
            .arg(extracted)
            .arg(failed)
            .arg(state)
    );
}

void MainWindow::consume_load_result() {
    auto result = m_load_watcher->future().takeResult();
    update_loading_indicator();
    if (m_drop_active_load_result) {
        m_drop_active_load_result = false;
        append_log(QCoreApplication::translate("MainWindow.Background", "Discarded completed load after clear"));
        if (!m_queued_load_paths.empty()) {
            auto queued = std::move(m_queued_load_paths);
            m_queued_load_paths.clear();
            start_loading_paths(std::move(queued));
            return;
        }
        m_loading_bar->hide();
        if (m_loading_status_label != nullptr) {
            m_loading_status_label->hide();
        }
        m_load_progress.reset();
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.Background", "Cleared loaded files"), 3000);
        return;
    }

    size_t added = 0;
    for (const auto& message : result.log_messages) {
        append_log(message);
    }
    m_file_model->reserve(m_file_model->rowCount() + static_cast<int>(result.loaded.size()));
    for (auto& [document, canonical] : result.loaded) {
        if (m_file_model->index_of_path(canonical) >= 0) {
            continue;
        }
        const auto format = utf8_to_qstring(localized_document_format(document));
        m_file_model->add_document(std::move(document), canonical);
        append_log(QCoreApplication::translate("MainWindow.Background", "Loaded: ") + canonical + QCoreApplication::translate("MainWindow.Background", " as ") + format);
        ++added;
    }

    if (!m_queued_load_paths.empty()) {
        auto queued = std::move(m_queued_load_paths);
        m_queued_load_paths.clear();
        start_loading_paths(std::move(queued));
        return;
    }

    m_loading_bar->hide();
    if (m_loading_status_label != nullptr) {
        m_loading_status_label->hide();
    }
    m_load_progress.reset();
    statusBar()->showMessage(
        QCoreApplication::translate("MainWindow.Background", "%1 files loaded from %2 valid, %3 rejected, %4 checked")
            .arg(added)
            .arg(result.loaded.size())
            .arg(result.rejected_count)
            .arg(result.candidate_count),
        6000
    );
}

void MainWindow::start_document_materialization(int row) {
    if (m_file_model == nullptr || row < 0) {
        return;
    }
    const auto* document = m_file_model->document_at(row);
    if (document == nullptr || document->summary_loaded) {
        return;
    }

    const auto canonical = m_file_model->canonical_path_at(row);
    if (canonical.isEmpty()) {
        return;
    }
    if (materialization_running()) {
        if (canonical != m_materialize_canonical_path) {
            m_pending_materialize_canonical_path = canonical;
        }
        return;
    }

    m_materialize_canonical_path = canonical;
    m_pending_materialize_canonical_path.clear();
    const auto request_id = ++m_materialize_request_id;
    auto keys = m_decryption_keys;
    LoadedDocument pending = *document;
    m_materialize_watcher->setFuture(QtConcurrent::run([pending = std::move(pending), canonical, keys = std::move(keys), request_id]() mutable {
        MaterializeResult result;
        result.canonical_path = canonical;
        result.request_id = request_id;
        if (auto loaded = materialize_document_summary(pending, result.rejection_reason, keys)) {
            result.document = std::move(*loaded);
        }
        return result;
    }));
}

void MainWindow::consume_materialize_result() {
    auto result = m_materialize_watcher->future().takeResult();
    m_materialize_canonical_path.clear();

    const auto row = m_file_model == nullptr ? -1 : m_file_model->index_of_path(result.canonical_path);
    if (result.request_id == m_materialize_request_id && row >= 0 && result.document.has_value()) {
        m_file_model->replace_document(row, std::move(*result.document));
        append_log(QCoreApplication::translate("MainWindow.Background", "Loaded details: ") + result.canonical_path);

        if (m_file_view != nullptr && m_file_proxy != nullptr && m_file_view->currentIndex().isValid()) {
            const auto current_source = m_file_proxy->mapToSource(m_file_view->currentIndex());
            if (current_source.isValid() && current_source.row() == row) {
                show_document(m_file_model->document_at(row));
            }
        }
    } else if (result.request_id == m_materialize_request_id && row >= 0) {
        const auto message = QCoreApplication::translate("MainWindow.Background", "Could not load document details: %1").arg(utf8_to_qstring(result.rejection_reason));
        append_log(message + QStringLiteral(" (") + result.canonical_path + QStringLiteral(")"));
        if (m_file_view != nullptr && m_file_proxy != nullptr && m_file_view->currentIndex().isValid()) {
            const auto current_source = m_file_proxy->mapToSource(m_file_view->currentIndex());
            if (current_source.isValid() && current_source.row() == row) {
                statusBar()->showMessage(message, 5000);
            }
        }
    }

    const auto pending = std::exchange(m_pending_materialize_canonical_path, {});
    if (!pending.isEmpty() && m_file_model != nullptr) {
        const auto pending_row = m_file_model->index_of_path(pending);
        if (pending_row >= 0) {
            start_document_materialization(pending_row);
        }
    }
}

void MainWindow::consume_extract_result() {
    auto report = m_extract_watcher->future().takeResult();
    update_extraction_indicator();
    if (!report.messages_logged_live) {
        for (const auto& message : report.messages) {
            append_log(utf8_to_qstring(message));
        }
    }
    if (report.diagnostic_path.has_value()) {
        append_log(QCoreApplication::translate("MainWindow.Background", "Extraction report: ") + path_to_qstring(*report.diagnostic_path));
    }

    m_loading_bar->hide();
    if (m_cancel_extraction_button != nullptr) {
        m_cancel_extraction_button->hide();
        m_cancel_extraction_button->setEnabled(true);
        m_cancel_extraction_button->setText(QCoreApplication::translate("MainWindow.Background", "Cancel"));
    }
    if (m_loading_status_label != nullptr) {
        m_loading_status_label->hide();
    }
    m_extract_progress.reset();
    const auto summary = report.diagnostic_path.has_value()
            ? QCoreApplication::translate("MainWindow.Background", "%1 extracted, %2 failed, %3 total · report: %4")
                .arg(report.extracted)
                .arg(report.failed)
                .arg(report.total)
                .arg(path_to_qstring(*report.diagnostic_path))
            : QCoreApplication::translate("MainWindow.Background", "%1 extracted, %2 failed, %3 total")
            .arg(report.extracted)
            .arg(report.failed)
            .arg(report.total);
    statusBar()->showMessage(
        report.canceled ? QCoreApplication::translate("MainWindow.Background", "Extraction canceled · %1").arg(summary) : summary,
        7000
    );
}

} // namespace cristudio
