#include "shared/i18n.hpp"
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
#include <utility>
#include <map>
#include <unordered_set>

namespace cristudio {
namespace {

[[nodiscard]] QString masking_inference(const UsmKeyRecoveryResult& result) {
    if (result.hca_video_supported) {
        return QCoreApplication::translate("MainWindow.UsmKeyRecovery", "Likely masked; embedded HCA and video recovery agree");
    }
    if (result.audio_chunks != 0u && result.audio_score > 0.0f) {
        return QCoreApplication::translate("MainWindow.UsmKeyRecovery", "Likely masked; ADX audio mask evidence was detected");
    }
    if (result.video_chunks != 0u) {
        return QCoreApplication::translate("MainWindow.UsmKeyRecovery", "Unknown; video model agreement is not proof of masking");
    }
    return QStringLiteral("Unknown");
}

[[nodiscard]] float evidence_rank_score(const UsmKeyRecoveryResult& result) noexcept {
    float score = result.score;
    if (result.audio_chunks != 0u) {
        score = std::max(score, result.audio_score);
    }
    if (result.hca_video_supported) {
        score = std::max(score, result.hca_score);
    }
    return score;
}

[[nodiscard]] std::vector<KeyRecoveryGroup> group_results(
    const std::vector<UsmKeyRecoveryResult>& results,
    size_t max_groups = 0) {
    std::vector<KeyRecoveryCandidate> candidates;
    candidates.reserve(results.size());
    std::unordered_set<std::string> ranked_sources;
    for (const auto& result : results) {
        const bool recommended = ranked_sources.insert(result.source).second;
        candidates.push_back(KeyRecoveryCandidate{
            .identity = result.key,
            .key = recovery_key_text(result.key, 14),
            .score = evidence_rank_score(result),
            .file = utf8_to_qstring(result.source),
            .recommended = recommended,
        });
    }
    return group_key_recovery_candidates(candidates, max_groups);
}

} // namespace

void MainWindow::start_usm_key_recovery(
    std::vector<UsmRecoverySource> sources,
    QString target_label
) {
    if (sources.empty()) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.UsmKeyRecovery", "No files selected for USM key recovery"), 3000);
        return;
    }
    if (key_recovery_running()) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.UsmKeyRecovery", "Key recovery is already running"), 3000);
        return;
    }
    const auto mode = choose_key_recovery_mode(
        this, QCoreApplication::translate("MainWindow.UsmKeyRecovery", "USM key recovery"), sources.size());
    if (!mode) return;
    if (sources.size() > 1u) {
        begin_key_recovery_progress(this, QCoreApplication::translate("MainWindow.UsmKeyRecovery", "USM key recovery"), sources.size());
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

    statusBar()->showMessage(QCoreApplication::translate("MainWindow.UsmKeyRecovery", "Recovering USM keys..."));
    const auto request_id = ++m_usm_key_recovery_request_id;
    auto keys = m_decryption_keys;
    m_usm_key_recovery_watcher->setFuture(QtConcurrent::run(
        [sources = std::move(sources), keys = std::move(keys), target_label = std::move(target_label),
         request_id, mode = *mode, window = QPointer<MainWindow>(this)]() mutable {
            UsmKeyRecoveryTaskResult task;
            task.target_label = std::move(target_label);
            task.request_id = request_id;
            task.requested_sources = sources.size();
            task.report.emplace();
            KeyRecoveryProgressThrottle progress_throttle;
            for (const auto& source : sources) {
                auto partial = recover_usm_keys(std::span<const UsmRecoverySource>(&source, 1), keys);
                if (partial) {
                    task.report->usm_count += partial->usm_count;
                    task.report->recovered.insert(
                        task.report->recovered.end(),
                        std::make_move_iterator(partial->recovered.begin()),
                        std::make_move_iterator(partial->recovered.end()));
                    task.report->errors.insert(
                        task.report->errors.end(),
                        std::make_move_iterator(partial->errors.begin()),
                        std::make_move_iterator(partial->errors.end()));
                } else {
                    task.report->errors.push_back(partial.error());
                }
                if (task.requested_sources > 1u && !window.isNull()) {
                    const size_t completed = std::min(
                        task.requested_sources,
                        task.report->usm_count + task.report->errors.size());
                    if (!progress_throttle.ready(completed, task.requested_sources)) continue;
                    auto groups = group_results(task.report->recovered, MaxInterimKeyRecoveryGroups);
                    auto* dispatcher = QCoreApplication::instance();
                    if (dispatcher == nullptr) continue;
                    QMetaObject::invokeMethod(dispatcher, [window, groups = std::move(groups), completed,
                        total = task.requested_sources]() mutable {
                        if (!window.isNull()) {
                            update_key_recovery_progress(
                                window, std::move(groups), completed, total,
                                QCoreApplication::translate("MainWindow.UsmKeyRecovery", "Processed %1 of %2 selected files.").arg(completed).arg(total));
                        }
                    }, Qt::QueuedConnection);
                }
            }
            if (task.report->usm_count == 0u) {
                task.error = task.report->errors.empty()
                    ? QCoreApplication::translate("MainWindow.UsmKeyRecovery", "No USM containers were found.")
                    : utf8_to_qstring(task.report->errors.front());
                task.report.reset();
                return task;
            }
            if (mode == cricodecs::KeyRecoveryMode::SharedBaseKey) {
                std::map<uint64_t, size_t> support;
                for (const auto& candidate : task.report->recovered) ++support[candidate.key];
                std::erase_if(task.report->recovered, [&](const auto& candidate) {
                    return support[candidate.key] != task.report->usm_count;
                });
                if (task.report->recovered.empty()) {
                    task.error = QCoreApplication::translate("MainWindow.UsmKeyRecovery", "No key candidate was shared by every recovered USM container.");
                    task.report.reset();
                }
            }
            return task;
        }
    ));
}

void MainWindow::consume_usm_key_recovery_result() {
    auto task = m_usm_key_recovery_watcher->future().takeResult();
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
    if (task.request_id != m_usm_key_recovery_request_id) {
        return;
    }

    if (!task.report) {
        if (task.requested_sources > 1u) {
            update_key_recovery_progress(
                this, {}, task.requested_sources, task.requested_sources, task.error, true);
        } else {
            show_key_recovery_error(this, QCoreApplication::translate("MainWindow.UsmKeyRecovery", "USM key recovery"), task.error);
        }
        append_log(QCoreApplication::translate("MainWindow.UsmKeyRecovery", "USM key recovery failed: ") + task.error);
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.UsmKeyRecovery", "USM key recovery unavailable"), 5000);
        return;
    }

    std::map<std::string, float> best_by_source;
    for (const auto& result : task.report->recovered) {
        auto& best = best_by_source[result.source];
        best = std::max(best, evidence_rank_score(result));
    }
    QString text;
    size_t visible_candidate_count = 0;
    for (const auto& result : task.report->recovered) {
        const float rank_score = evidence_rank_score(result);
        if (!show_key_recovery_candidate(rank_score, best_by_source[result.source])) continue;
        ++visible_candidate_count;
        const auto key = recovery_key_text(result.key, 14);
        const auto score = QString::number(rank_score, 'f', 6);
        text += QCoreApplication::translate(
                    "MainWindow.UsmKeyRecovery",
                    "%1\nKey: %2\nEvidence rank score: %3\nVideo model score: %4\nMasking: %5\n"
                    "Video chunks: %6\nADX audio chunks: %7\nADX audio score: %8\n"
                    "HCA streams: %9\nHCA score: %10\nHCA/video agreement: %11\nSample blocks: %12\n\n")
                    .arg(utf8_to_qstring(result.source))
                    .arg(key)
                    .arg(score)
                    .arg(QString::number(result.score, 'f', 6))
                    .arg(masking_inference(result))
                    .arg(result.video_chunks)
                    .arg(result.audio_chunks)
                    .arg(QString::number(result.audio_score, 'f', 6))
                    .arg(result.hca_streams)
                    .arg(QString::number(result.hca_score, 'f', 6))
                    .arg(result.hca_video_supported ? QStringLiteral("yes") : QStringLiteral("no"))
                    .arg(result.sample_blocks);
        append_log(QCoreApplication::translate("MainWindow.UsmKeyRecovery", "USM key recovery completed for %1: %2, score %3")
                       .arg(utf8_to_qstring(result.source), key, score));
    }
    if (!task.report->errors.empty()) {
        text += QCoreApplication::translate("MainWindow.UsmKeyRecovery", "Unavailable targets\n");
        for (const auto& error : task.report->errors) {
            text += QStringLiteral("- %1\n").arg(utf8_to_qstring(error));
        }
        text += QLatin1Char('\n');
    }
    text += QCoreApplication::translate(
        "MainWindow.UsmKeyRecovery",
        "Candidates were not applied to the global CRI key.\n"
        "The evidence rank uses the strongest applicable video, ADX, or HCA score; "
        "it is not proof that a USM is masked.\n"
        "CRI key recovery returns only the effective low 56 bits; "
        "the original upper byte is unrecoverable.");
    auto groups = group_results(task.report->recovered);
    QStringList keys;
    keys.reserve(static_cast<qsizetype>(groups.size()));
    for (const auto& group : groups) {
        keys.push_back(group.key);
    }
    const auto candidate_count = visible_candidate_count;
    const auto summary = candidate_count == 0
        ? QCoreApplication::translate("MainWindow.UsmKeyRecovery", "No key candidate was recovered.")
        : (candidate_count == 1
            ? QCoreApplication::translate("MainWindow.UsmKeyRecovery", "Recovered key: %1\nScore: %2")
                  .arg(keys.front())
                  .arg(QString::number(groups.front().mean_score, 'f', 6))
            : QCoreApplication::translate("MainWindow.UsmKeyRecovery", "Recovered %1 candidates in %2 exact-key groups.")
                  .arg(candidate_count)
                  .arg(groups.size()));
    if (task.requested_sources > 1u) {
        update_key_recovery_progress(
            this, groups, task.requested_sources, task.requested_sources, summary, true);
    } else {
        show_key_recovery_result(
            this,
            QCoreApplication::translate("MainWindow.UsmKeyRecovery", "USM key recovery"),
            summary,
            QCoreApplication::translate("MainWindow.UsmKeyRecovery", "Target: %1\nUSM containers: %2\n\n%3")
                .arg(task.target_label)
                .arg(task.report->usm_count)
                .arg(text),
            keys.join(QLatin1Char('\n')),
            groups.size() == 1 ? QCoreApplication::translate("MainWindow.UsmKeyRecovery", "Copy Key") : QCoreApplication::translate("MainWindow.UsmKeyRecovery", "Copy Keys"),
            std::move(groups)
        );
    }
    statusBar()->showMessage(
        task.report->recovered.empty()
            ? QCoreApplication::translate("MainWindow.UsmKeyRecovery", "USM key recovery found no candidate")
            : QCoreApplication::translate("MainWindow.UsmKeyRecovery", "USM key recovery completed"),
        5000);
}

} // namespace cristudio
