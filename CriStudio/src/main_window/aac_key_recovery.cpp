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

#include <utility>

namespace cristudio {
namespace {

} // namespace

void MainWindow::start_aac_key_recovery(
    std::vector<AacRecoverySource> sources,
    QString target_label
) {
    if (sources.empty()) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.AacKeyRecovery", "No AAC/M4A sources selected for key recovery"), 3000);
        return;
    }
    if (key_recovery_running()) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.AacKeyRecovery", "Key recovery is already running"), 3000);
        return;
    }
    const auto mode = choose_key_recovery_mode(
        this, QCoreApplication::translate("MainWindow.AacKeyRecovery", "AAC key recovery"), sources.size());
    if (!mode) return;
    if (sources.size() > 1u) {
        begin_key_recovery_progress(this, QCoreApplication::translate("MainWindow.AacKeyRecovery", "AAC key recovery"), sources.size());
    }

    if (m_preview_recover_key_button != nullptr) m_preview_recover_key_button->setEnabled(false);
    if (m_preview_recover_usm_key_button != nullptr) m_preview_recover_usm_key_button->setEnabled(false);
    if (m_preview_recover_adx_key_button != nullptr) m_preview_recover_adx_key_button->setEnabled(false);
    if (m_preview_recover_aac_key_button != nullptr) m_preview_recover_aac_key_button->setEnabled(false);

    statusBar()->showMessage(QCoreApplication::translate("MainWindow.AacKeyRecovery", "Recovering AAC key..."));
    const auto request_id = ++m_aac_key_recovery_request_id;
    m_aac_key_recovery_watcher->setFuture(QtConcurrent::run(
        [sources = std::move(sources), target_label = std::move(target_label), request_id,
         mode = *mode, window = QPointer<MainWindow>(this)]() mutable {
            AacKeyRecoveryTaskResult task;
            task.target_label = std::move(target_label);
            task.request_id = request_id;
            task.requested_sources = sources.size();
            std::vector<KeyRecoveryCandidate> displayed;
            KeyRecoveryProgressThrottle progress_throttle;
            const auto publish = [&](QString status) {
                if (task.requested_sources <= 1u || window.isNull()) return;
                const size_t completed = mode == cricodecs::KeyRecoveryMode::SharedBaseKey
                    ? task.requested_sources
                    : task.recovered.size() + task.errors.size();
                if (!progress_throttle.ready(completed, task.requested_sources)) return;
                auto groups = group_key_recovery_candidates(displayed, MaxInterimKeyRecoveryGroups);
                auto* dispatcher = QCoreApplication::instance();
                if (dispatcher == nullptr) return;
                QMetaObject::invokeMethod(dispatcher, [window, groups = std::move(groups), completed,
                    total = task.requested_sources, status = std::move(status)]() mutable {
                    if (!window.isNull()) {
                        update_key_recovery_progress(window, std::move(groups), completed, total, std::move(status));
                    }
                }, Qt::QueuedConnection);
            };
            const auto append_recovered = [&](AacKeyRecoveryResult recovered, QString label) {
                const float best_score = recovered.candidates.empty() ? 0.0f : recovered.candidates.front().score;
                for (const auto& candidate : recovered.candidates) {
                    if (!show_key_recovery_candidate(candidate.score, best_score)) continue;
                    displayed.push_back(KeyRecoveryCandidate{
                        .identity = candidate.key,
                        .key = recovery_key_text(candidate.key, 13),
                        .score = candidate.score,
                        .file = label,
                    });
                }
                task.recovered.push_back(AacRecoveredTarget{
                    .recovered = std::move(recovered),
                    .source = std::move(label),
                });
            };
            if (mode == cricodecs::KeyRecoveryMode::SharedBaseKey) {
                auto recovered = recover_aac_key(sources);
                if (recovered) {
                    append_recovered(std::move(*recovered), task.target_label);
                } else {
                    task.errors.push_back(utf8_to_qstring(recovered.error()));
                }
                publish(QCoreApplication::translate("MainWindow.AacKeyRecovery", "Shared-base recovery completed."));
            } else {
                task.recovered.reserve(sources.size());
                for (const auto& source : sources) {
                    const auto label = recovery_source_label(
                        source.name, source.path, "MainWindow.AacKeyRecovery");
                    auto recovered = recover_aac_key(std::span<const AacRecoverySource>(&source, 1));
                    if (recovered) {
                        append_recovered(std::move(*recovered), label);
                    } else {
                        task.errors.push_back(label + QStringLiteral(": ") + utf8_to_qstring(recovered.error()));
                    }
                    publish(QCoreApplication::translate("MainWindow.AacKeyRecovery", "Processed %1 of %2 selected files.")
                        .arg(task.recovered.size() + task.errors.size()).arg(task.requested_sources));
                }
            }
            return task;
        }
    ));
}

void MainWindow::consume_aac_key_recovery_result() {
    auto task = m_aac_key_recovery_watcher->future().takeResult();
    if (m_preview_recover_key_button != nullptr) m_preview_recover_key_button->setEnabled(true);
    if (m_preview_recover_usm_key_button != nullptr) m_preview_recover_usm_key_button->setEnabled(true);
    if (m_preview_recover_adx_key_button != nullptr) m_preview_recover_adx_key_button->setEnabled(true);
    if (m_preview_recover_aac_key_button != nullptr) m_preview_recover_aac_key_button->setEnabled(true);
    if (task.request_id != m_aac_key_recovery_request_id) {
        return;
    }

    if (task.recovered.empty()) {
        const auto error = task.errors.empty()
            ? QCoreApplication::translate("MainWindow.AacKeyRecovery", "No encrypted AAC sources were found.")
            : task.errors.front();
        if (task.requested_sources > 1u) {
            update_key_recovery_progress(
                this, {}, task.requested_sources, task.requested_sources, error, true);
        } else {
            show_key_recovery_error(this, QCoreApplication::translate("MainWindow.AacKeyRecovery", "AAC key recovery"), error);
        }
        append_log(QCoreApplication::translate("MainWindow.AacKeyRecovery", "AAC key recovery failed: ") + error);
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.AacKeyRecovery", "AAC key recovery unavailable"), 5000);
        return;
    }

    std::vector<KeyRecoveryCandidate> candidates;
    candidates.reserve(task.recovered.size() * cricodecs::MaxKeyRecoveryCandidates);
    QString details = QCoreApplication::translate("MainWindow.AacKeyRecovery", "Target: %1\n\n").arg(task.target_label);
    size_t source_count = 0;
    for (const auto& target : task.recovered) {
        const float best_score = target.recovered.candidates.empty()
            ? 0.0f
            : target.recovered.candidates.front().score;
        for (const auto& recovered : target.recovered.candidates) {
            if (!show_key_recovery_candidate(recovered.score, best_score)) continue;
            const auto key = recovery_key_text(recovered.key, 13);
            const auto score = QString::number(recovered.score, 'f', 6);
            candidates.push_back(KeyRecoveryCandidate{
                .identity = recovered.key,
                .key = key,
                .score = recovered.score,
                .file = target.source,
            });
            details += QCoreApplication::translate(
                "MainWindow.AacKeyRecovery",
                "%1\nKey: %2\nScore: %3\nValidated AAC sources: %4 of %5\n"
                "Containers: %6\nCandidates: %7\n\n")
                .arg(target.source, key, score)
                .arg(recovered.validated_sources)
                .arg(recovered.source_count)
                .arg(target.recovered.container_count)
                .arg(recovered.candidate_count);
            append_log(QCoreApplication::translate(
                "MainWindow.AacKeyRecovery",
                "AAC key recovery candidate for %1: %2, score %3, %n source(s)",
                nullptr,
                static_cast<int>(recovered.source_count))
                .arg(target.source, key, score));
        }
        source_count += target.recovered.source_count;
    }
    if (!task.errors.empty()) {
        details += QCoreApplication::translate("MainWindow.AacKeyRecovery", "Unavailable targets\n");
        for (const auto& error : task.errors) {
            details += QStringLiteral("- %1\n").arg(error);
        }
        details += QLatin1Char('\n');
    }
    details += QCoreApplication::translate(
        "MainWindow.AacKeyRecovery",
        "Candidates were not applied globally. Scores are normalized MP4 structural agreement, not probability. "
        "Exact keys are grouped and ranked by score, file support, filename, and key.");

    auto groups = group_key_recovery_candidates(candidates);
    QStringList keys;
    keys.reserve(static_cast<qsizetype>(groups.size()));
    for (const auto& group : groups) {
        keys.push_back(group.key);
    }
    const auto summary = groups.empty()
        ? QCoreApplication::translate("MainWindow.AacKeyRecovery", "No positive key candidate was recovered.")
        : task.recovered.size() == 1
        ? QCoreApplication::translate("MainWindow.AacKeyRecovery", "Recovered key: %1\nScore: %2")
              .arg(groups.front().key)
              .arg(QString::number(groups.front().mean_score, 'f', 6))
        : QCoreApplication::translate("MainWindow.AacKeyRecovery", "Recovered %1 files in %2 exact-key groups.")
              .arg(task.recovered.size())
              .arg(groups.size());
    if (task.requested_sources > 1u) {
        update_key_recovery_progress(
            this, groups, task.requested_sources, task.requested_sources, summary, true);
    } else {
        show_key_recovery_result(
            this,
            QCoreApplication::translate("MainWindow.AacKeyRecovery", "AAC key recovery"),
            summary,
            details,
            keys.join(QLatin1Char('\n')),
            groups.size() == 1 ? QCoreApplication::translate("MainWindow.AacKeyRecovery", "Copy Key") : QCoreApplication::translate("MainWindow.AacKeyRecovery", "Copy Keys"),
            std::move(groups)
        );
    }
    statusBar()->showMessage(
        QCoreApplication::translate("MainWindow.AacKeyRecovery", "AAC key recovery completed. Files: %1. Sources: %2.")
            .arg(task.recovered.size())
            .arg(source_count),
        5000);
}

} // namespace cristudio
