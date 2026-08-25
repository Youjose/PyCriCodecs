#include "../main_window.hpp"

#include "../path_text.hpp"
#include "ui_helpers.hpp"

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QMetaObject>
#include <QPointer>
#include <QStatusBar>
#include <QStringList>
#include <QToolButton>
#include <QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

namespace cristudio {
namespace {

} // namespace

void MainWindow::start_hca_key_recovery(
    std::vector<HcaRecoverySource> sources,
    QString target_label
) {
    if (sources.empty()) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.HcaKeyRecovery", "No files selected for HCA key recovery"), 3000);
        return;
    }
    if (key_recovery_running()) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.HcaKeyRecovery", "Key recovery is already running"), 3000);
        return;
    }
    const auto mode = choose_key_recovery_mode(
        this, QCoreApplication::translate("MainWindow.HcaKeyRecovery", "HCA key recovery"), sources.size());
    if (!mode) {
        return;
    }

    const auto request_id = ++m_hca_key_recovery_request_id;
    m_hca_key_recovery_stop_source = std::stop_source{};
    const auto stop_token = m_hca_key_recovery_stop_source.get_token();
    if (sources.size() > 1) {
        begin_key_recovery_progress(
            this,
            QCoreApplication::translate("MainWindow.HcaKeyRecovery", "HCA key recovery"),
            sources.size(),
            [this, request_id] {
                if (m_hca_key_recovery_watcher->isRunning() && request_id == m_hca_key_recovery_request_id) {
                    m_hca_key_recovery_stop_source.request_stop();
                    statusBar()->showMessage(QCoreApplication::translate("MainWindow.HcaKeyRecovery", "Canceling HCA key recovery..."));
                }
            });
    }

    if (m_preview_recover_key_button != nullptr) {
        m_preview_recover_key_button->setEnabled(false);
    }
    if (m_preview_recover_usm_key_button != nullptr) {
        m_preview_recover_usm_key_button->setEnabled(false);
    }
    if (m_preview_recover_adx_key_button != nullptr) {
        m_preview_recover_adx_key_button->setEnabled(false);
    }
    if (m_preview_recover_aac_key_button != nullptr) {
        m_preview_recover_aac_key_button->setEnabled(false);
    }

    statusBar()->showMessage(QCoreApplication::translate("MainWindow.HcaKeyRecovery", "Recovering HCA keys..."));
    auto keys = m_decryption_keys;
    m_hca_key_recovery_watcher->setFuture(QtConcurrent::run(
        [sources = std::move(sources), keys = std::move(keys), target_label = std::move(target_label),
         request_id, mode = *mode, stop_token, window = QPointer<MainWindow>(this)]() mutable {
            HcaKeyRecoveryTaskResult task;
            task.target_label = std::move(target_label);
            task.request_id = request_id;
            task.requested_sources = sources.size();
            std::vector<KeyRecoveryCandidate> displayed;
            KeyRecoveryProgressThrottle progress_throttle;
            const auto publish = [&](QString status) {
                if (task.requested_sources <= 1 || window.isNull()) return;
                const size_t completed = task.recovered.size() + task.errors.size();
                if (!progress_throttle.ready(completed, task.requested_sources)) return;
                const auto groups = group_key_recovery_candidates(displayed, MaxInterimKeyRecoveryGroups);
                auto* dispatcher = QCoreApplication::instance();
                if (dispatcher == nullptr) return;
                QMetaObject::invokeMethod(dispatcher, [window, groups, completed, total = task.requested_sources, status = std::move(status)]() mutable {
                    if (!window.isNull()) {
                        update_key_recovery_progress(window, std::move(groups), completed, total, std::move(status));
                    }
                }, Qt::QueuedConnection);
            };
            const auto append_displayed = [&](const HcaRecoveredTarget& target) {
                const float best_score = target.recovered.recovery.candidates.empty()
                    ? 0.0f
                    : target.recovered.recovery.candidates.front().score;
                for (const auto& candidate : target.recovered.recovery.candidates) {
                    if (!show_key_recovery_candidate(candidate.score, best_score)) continue;
                    displayed.push_back(KeyRecoveryCandidate{
                        .identity = candidate.key,
                        .key = recovery_key_text(candidate.key, 14),
                        .score = candidate.score,
                        .file = target.source,
                    });
                }
            };
            if (mode == cricodecs::KeyRecoveryMode::SharedBaseKey) {
                KeyRecoveryProgressThrottle collection_throttle;
                KeyRecoveryProgressThrottle group_throttle;
                cricodecs::hca::KeyRecoveryOptions options;
                options.mode = mode;
                options.stop_token = stop_token;
                options.progress = [window, &collection_throttle, &group_throttle](
                    const cricodecs::hca::KeyRecoveryProgress& progress) {
                    if (window.isNull()) {
                        return;
                    }
                    auto& throttle = progress.stage == cricodecs::hca::KeyRecoveryStage::Collecting
                        ? collection_throttle
                        : group_throttle;
                    if (!throttle.ready(progress.completed, progress.total)) {
                        return;
                    }
                    const auto status = progress.stage == cricodecs::hca::KeyRecoveryStage::Collecting
                        ? QCoreApplication::translate("MainWindow.HcaKeyRecovery", "Scanning %1 of %2 selected files; found %3 encrypted HCA streams.")
                              .arg(progress.completed)
                              .arg(progress.total)
                              .arg(progress.source_count)
                        : QCoreApplication::translate("MainWindow.HcaKeyRecovery", "Recovered %1 of %2 AWB subkey groups; %3 groups produced candidates from %4 HCA streams.")
                              .arg(progress.completed)
                              .arg(progress.total)
                              .arg(progress.resolved_groups)
                              .arg(progress.source_count);
                    auto* dispatcher = QCoreApplication::instance();
                    if (dispatcher == nullptr) {
                        return;
                    }
                    QMetaObject::invokeMethod(dispatcher, [window, completed = progress.completed,
                                              total = progress.total, status]() mutable {
                        if (!window.isNull()) {
                            update_key_recovery_progress(
                                window, {}, completed, total, std::move(status));
                        }
                    }, Qt::QueuedConnection);
                };
                auto recovered = recover_hca_key(sources, keys, options);
                if (recovered) {
                    task.recovered.push_back(HcaRecoveredTarget{
                        .recovered = *recovered,
                        .source = task.target_label,
                    });
                    append_displayed(task.recovered.back());
                } else if (stop_token.stop_requested() ||
                           recovered.error() == "HCA key recovery canceled") {
                    task.canceled = true;
                } else {
                    task.errors.push_back(utf8_to_qstring(recovered.error()));
                }
            } else {
                struct SourceResult {
                    std::optional<HcaKeyRecoveryResult> recovered;
                    QString label;
                    QString error;
                };
                std::vector<SourceResult> results(sources.size());
                std::atomic_size_t next_source = 0;
                std::atomic_size_t completed_sources = 0;
                std::mutex displayed_mutex;
                const auto hardware_threads = std::max(1u, std::thread::hardware_concurrency());
                const auto worker_count = std::min<size_t>({4, sources.size(), hardware_threads});
                std::vector<std::jthread> workers;
                workers.reserve(worker_count);
                for (size_t worker = 0; worker < worker_count; ++worker) {
                    workers.emplace_back([&] {
                        while (true) {
                            if (stop_token.stop_requested()) {
                                return;
                            }
                            const auto index = next_source.fetch_add(1, std::memory_order_relaxed);
                            if (index >= sources.size()) {
                                return;
                            }
                            const auto& source = sources[index];
                            auto& result = results[index];
                            result.label = recovery_source_label(
                                source.name, source.path, "MainWindow.HcaKeyRecovery");
                            cricodecs::hca::KeyRecoveryOptions options;
                            options.mode = mode;
                            options.worker_count = 1;
                            options.stop_token = stop_token;
                            auto recovered = recover_hca_key(
                                std::span<const HcaRecoverySource>(&source, 1), keys, options);
                            if (recovered) {
                                result.recovered = std::move(*recovered);
                            } else if (stop_token.stop_requested() ||
                                       recovered.error() == "HCA key recovery canceled") {
                                return;
                            } else {
                                result.error = result.label + QStringLiteral(": ") + utf8_to_qstring(recovered.error());
                            }

                            {
                                const std::scoped_lock lock(displayed_mutex);
                                if (result.recovered) {
                                    const auto& candidates = result.recovered->recovery.candidates;
                                    const float best_score = candidates.empty() ? 0.0f : candidates.front().score;
                                    for (const auto& candidate : candidates) {
                                        if (!show_key_recovery_candidate(candidate.score, best_score)) continue;
                                        displayed.push_back(KeyRecoveryCandidate{
                                            .identity = candidate.key,
                                            .key = recovery_key_text(candidate.key, 14),
                                            .score = candidate.score,
                                            .file = result.label,
                                        });
                                    }
                                }
                                const auto completed = completed_sources.fetch_add(1, std::memory_order_relaxed) + 1;
                                auto groups = group_key_recovery_candidates(displayed, MaxInterimKeyRecoveryGroups);
                                if (!window.isNull()) {
                                    if (auto* dispatcher = QCoreApplication::instance(); dispatcher != nullptr) {
                                        QMetaObject::invokeMethod(dispatcher, [window, groups = std::move(groups), completed, total = sources.size()]() mutable {
                                            if (!window.isNull()) {
                                                update_key_recovery_progress(
                                                    window, std::move(groups), completed, total,
                                                    QCoreApplication::translate("MainWindow.HcaKeyRecovery", "Processed %1 of %2 selected files.")
                                                        .arg(completed)
                                                        .arg(total));
                                            }
                                        }, Qt::QueuedConnection);
                                    }
                                }
                            }
                        }
                    });
                }
                workers.clear();
                task.canceled = stop_token.stop_requested();

                task.recovered.reserve(sources.size());
                for (auto& result : results) {
                    if (result.recovered) {
                        task.recovered.push_back(HcaRecoveredTarget{
                            .recovered = std::move(*result.recovered),
                            .source = std::move(result.label),
                        });
                    } else if (!result.error.isEmpty()) {
                        task.errors.push_back(std::move(result.error));
                    }
                }
            }
            return task;
        }
    ));
}

void MainWindow::consume_hca_key_recovery_result() {
    auto task = m_hca_key_recovery_watcher->future().takeResult();
    if (m_preview_recover_key_button != nullptr) {
        m_preview_recover_key_button->setEnabled(true);
    }
    if (m_preview_recover_usm_key_button != nullptr) {
        m_preview_recover_usm_key_button->setEnabled(true);
    }
    if (m_preview_recover_adx_key_button != nullptr) {
        m_preview_recover_adx_key_button->setEnabled(true);
    }
    if (m_preview_recover_aac_key_button != nullptr) {
        m_preview_recover_aac_key_button->setEnabled(true);
    }
    if (task.request_id != m_hca_key_recovery_request_id) {
        return;
    }
    if (task.canceled) {
        if (task.requested_sources > 1) {
            update_key_recovery_progress(
                this, {}, 1, 1, QCoreApplication::translate("MainWindow.HcaKeyRecovery", "HCA key recovery canceled."), true);
        }
        append_log(QCoreApplication::translate("MainWindow.HcaKeyRecovery", "HCA key recovery canceled"));
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.HcaKeyRecovery", "HCA key recovery canceled"), 3000);
        return;
    }

    if (task.recovered.empty()) {
        const auto error = task.errors.empty()
            ? QCoreApplication::translate("MainWindow.HcaKeyRecovery", "No cipher type-56 HCA streams were found.")
            : task.errors.front();
        if (task.requested_sources > 1) {
            update_key_recovery_progress(
                this, {}, task.requested_sources, task.requested_sources, error, true);
        } else {
            show_key_recovery_error(this, QCoreApplication::translate("MainWindow.HcaKeyRecovery", "HCA key recovery"), error);
        }
        append_log(QCoreApplication::translate("MainWindow.HcaKeyRecovery", "HCA key recovery failed: ") + error);
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.HcaKeyRecovery", "HCA key recovery unavailable"), 5000);
        return;
    }

    std::vector<KeyRecoveryCandidate> candidates;
    candidates.reserve(task.recovered.size() * cricodecs::MaxKeyRecoveryCandidates);
    QString details = QCoreApplication::translate("MainWindow.HcaKeyRecovery", "Target: %1\n\n").arg(task.target_label);
    size_t hca_count = 0;
    size_t best_support = 0;
    for (const auto& target : task.recovered) {
        const float best_score = target.recovered.recovery.candidates.empty()
            ? 0.0f
            : target.recovered.recovery.candidates.front().score;
        for (const auto& recovered : target.recovered.recovery.candidates) {
            if (!show_key_recovery_candidate(recovered.score, best_score)) continue;
            const auto key = recovery_key_text(recovered.key, 14);
            const auto score = QString::number(recovered.score, 'f', 6);
            candidates.push_back(KeyRecoveryCandidate{
                .identity = recovered.key,
                .key = key,
                .score = recovered.score,
                .file = target.source,
            });
            if (candidates.size() == 1) {
                best_support = recovered.source_count;
            }
            details += QCoreApplication::translate("MainWindow.HcaKeyRecovery", "%1\nKey: %2\nScore: %3\nSources: %4\nEvidence: %5\nEquivalents: %6\n\n")
                .arg(target.source, key, score)
                .arg(recovered.source_count)
                .arg(recovered.evidence_count)
                .arg(recovered.equivalent_count);
            append_log(QCoreApplication::translate(
                "MainWindow.HcaKeyRecovery",
                "HCA key recovery candidate for %1: %2, score %3, %n equivalent(s)",
                nullptr,
                static_cast<int>(recovered.equivalent_count))
                .arg(target.source, key, score));
        }
        hca_count += target.recovered.hca_count;
    }
    if (!task.errors.empty()) {
        details += QCoreApplication::translate("MainWindow.HcaKeyRecovery", "Unavailable targets\n");
        for (const auto& error : task.errors) {
            details += QStringLiteral("- %1\n").arg(error);
        }
        details += QLatin1Char('\n');
    }
    details += QCoreApplication::translate(
        "MainWindow.HcaKeyRecovery",
        "Candidates were not applied globally. Scores are normalized structural agreement, not probability. "
        "Rows are ranked by score, file support, filename, and key.\n"
        "CRI key recovery returns only the effective low 56 bits; the original upper byte is unrecoverable.");

    auto groups = group_key_recovery_candidates(candidates);
    QStringList keys;
    keys.reserve(static_cast<qsizetype>(groups.size()));
    for (const auto& group : groups) {
        keys.push_back(group.key);
    }
    const auto summary = groups.empty()
        ? QCoreApplication::translate("MainWindow.HcaKeyRecovery", "No positive key candidate was recovered.")
        : task.recovered.size() == 1
        ? QCoreApplication::translate("MainWindow.HcaKeyRecovery", "Recovered key: %1\nScore: %2\nHCA support: %3 of %4 streams")
              .arg(groups.front().key)
              .arg(QString::number(groups.front().mean_score, 'f', 6))
              .arg(best_support)
              .arg(hca_count)
        : QCoreApplication::translate("MainWindow.HcaKeyRecovery", "Recovered %1 files in %2 exact-key groups.")
              .arg(task.recovered.size())
              .arg(groups.size());
    if (task.requested_sources > 1) {
        update_key_recovery_progress(
            this,
            groups,
            task.requested_sources,
            task.requested_sources,
            summary,
            true);
    } else {
        show_key_recovery_result(
            this,
            QCoreApplication::translate("MainWindow.HcaKeyRecovery", "HCA key recovery"),
            summary,
            details,
            keys.join(QLatin1Char('\n')),
            groups.size() == 1 ? QCoreApplication::translate("MainWindow.HcaKeyRecovery", "Copy Key") : QCoreApplication::translate("MainWindow.HcaKeyRecovery", "Copy Keys"),
            std::move(groups)
        );
    }
    statusBar()->showMessage(
        QCoreApplication::translate("MainWindow.HcaKeyRecovery", "HCA key recovery completed. Files: %1. Streams: %2.")
            .arg(task.recovered.size())
            .arg(hca_count),
        5000);
}

} // namespace cristudio
