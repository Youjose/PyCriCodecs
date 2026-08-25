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

#include <utility>

namespace cristudio {
namespace {

[[nodiscard]] QString recovery_name(AdxRecoveryKind kind) {
    return kind == AdxRecoveryKind::Ahx ? QStringLiteral("AHX") : QStringLiteral("ADX");
}

[[nodiscard]] QString triplet_text(uint16_t start, uint16_t mult, uint16_t add) {
    return QStringLiteral("%1,%2,%3")
        .arg(start, 4, 16, QLatin1Char('0'))
        .arg(mult, 4, 16, QLatin1Char('0'))
        .arg(add, 4, 16, QLatin1Char('0'))
        .toUpper();
}

[[nodiscard]] uint64_t triplet_identity(uint16_t start, uint16_t mult, uint16_t add) noexcept {
    return (static_cast<uint64_t>(start) << 32u) |
        (static_cast<uint64_t>(mult) << 16u) | add;
}

[[nodiscard]] QString frames_text(const std::vector<uint64_t>& frames) {
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(frames.size()));
    for (const auto count : frames) {
        parts.push_back(QString::number(static_cast<qulonglong>(count)));
    }
    return parts.join(QLatin1Char(','));
}

} // namespace

void MainWindow::start_adx_key_recovery(
    std::vector<AdxRecoverySource> sources,
    AdxRecoveryKind kind,
    QString target_label
) {
    const auto name = recovery_name(kind);
    if (sources.empty()) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.AdxKeyRecovery", "No files selected for %1 key recovery").arg(name), 3000);
        return;
    }
    if (key_recovery_running()) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.AdxKeyRecovery", "Key recovery is already running"), 3000);
        return;
    }
    const auto mode = choose_key_recovery_mode(
        this, QCoreApplication::translate("MainWindow.AdxKeyRecovery", "%1 key recovery").arg(name), sources.size());
    if (!mode) return;
    if (sources.size() > 1u) {
        begin_key_recovery_progress(this, QCoreApplication::translate("MainWindow.AdxKeyRecovery", "%1 key recovery").arg(name), sources.size());
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

    statusBar()->showMessage(QCoreApplication::translate("MainWindow.AdxKeyRecovery", "Recovering %1 keys...").arg(name));
    const auto request_id = ++m_adx_key_recovery_request_id;
    auto keys = m_decryption_keys;
    m_adx_key_recovery_watcher->setFuture(QtConcurrent::run(
        [sources = std::move(sources), keys = std::move(keys), kind,
         target_label = std::move(target_label), request_id, mode = *mode,
         window = QPointer<MainWindow>(this)]() mutable {
            AdxKeyRecoveryTaskResult task;
            task.kind = kind;
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
            const auto append_recovered = [&](AdxKeyRecoveryResult recovered, QString label) {
                const float best_score = recovered.candidates.empty() ? 0.0f : recovered.candidates.front().score;
                for (const auto& candidate : recovered.candidates) {
                    if (!show_key_recovery_candidate(candidate.score, best_score)) continue;
                    displayed.push_back(KeyRecoveryCandidate{
                        .identity = triplet_identity(candidate.start, candidate.mult, candidate.add),
                        .key = triplet_text(candidate.start, candidate.mult, candidate.add),
                        .score = candidate.score,
                        .file = label,
                    });
                }
                task.recovered.push_back(AdxRecoveredTarget{
                    .recovered = std::move(recovered),
                    .source = std::move(label),
                });
            };
            if (mode == cricodecs::KeyRecoveryMode::SharedBaseKey) {
                auto recovered = recover_adx_key(sources, kind, keys);
                if (recovered) {
                    append_recovered(std::move(*recovered), task.target_label);
                } else {
                    task.errors.push_back(utf8_to_qstring(recovered.error()));
                }
                publish(QCoreApplication::translate("MainWindow.AdxKeyRecovery", "Shared-base recovery completed."));
            } else {
                task.recovered.reserve(sources.size());
                for (const auto& source : sources) {
                    const auto label = recovery_source_label(
                        source.name, source.path, "MainWindow.AdxKeyRecovery");
                    auto recovered = recover_adx_key(
                        std::span<const AdxRecoverySource>(&source, 1), kind, keys);
                    if (recovered) {
                        append_recovered(std::move(*recovered), label);
                    } else {
                        task.errors.push_back(label + QStringLiteral(": ") + utf8_to_qstring(recovered.error()));
                    }
                    publish(QCoreApplication::translate("MainWindow.AdxKeyRecovery", "Processed %1 of %2 selected files.")
                        .arg(task.recovered.size() + task.errors.size()).arg(task.requested_sources));
                }
            }
            return task;
        }
    ));
}

void MainWindow::consume_adx_key_recovery_result() {
    auto task = m_adx_key_recovery_watcher->future().takeResult();
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
    if (task.request_id != m_adx_key_recovery_request_id) {
        return;
    }

    const auto name = recovery_name(task.kind);
    if (task.recovered.empty()) {
        const auto error = task.errors.empty()
            ? QCoreApplication::translate("MainWindow.AdxKeyRecovery", "No encrypted %1 streams were found.").arg(name)
            : task.errors.front();
        if (task.requested_sources > 1u) {
            update_key_recovery_progress(
                this, {}, task.requested_sources, task.requested_sources, error, true);
        } else {
            show_key_recovery_error(this, QCoreApplication::translate("MainWindow.AdxKeyRecovery", "%1 key recovery").arg(name), error);
        }
        append_log(QCoreApplication::translate("MainWindow.AdxKeyRecovery", "%1 key recovery failed: %2").arg(name, error));
        statusBar()->showMessage(QCoreApplication::translate("MainWindow.AdxKeyRecovery", "%1 key recovery unavailable").arg(name), 5000);
        return;
    }

    std::vector<KeyRecoveryCandidate> candidates;
    candidates.reserve(task.recovered.size() * cricodecs::MaxKeyRecoveryCandidates);
    QString details = QCoreApplication::translate("MainWindow.AdxKeyRecovery", "Target: %1\n\n").arg(task.target_label);
    size_t stream_count = 0;
    for (const auto& target : task.recovered) {
        const auto& recovered = target.recovered;
        const auto triplet = triplet_text(recovered.start, recovered.mult, recovered.add);
        const auto score = QString::number(recovered.score, 'f', 6);
        const float best_score = recovered.candidates.empty() ? 0.0f : recovered.candidates.front().score;
        for (const auto& candidate : recovered.candidates) {
            if (!show_key_recovery_candidate(candidate.score, best_score)) continue;
            candidates.push_back(KeyRecoveryCandidate{
                .identity = triplet_identity(candidate.start, candidate.mult, candidate.add),
                .key = triplet_text(candidate.start, candidate.mult, candidate.add),
                .score = candidate.score,
                .file = target.source,
            });
        }
        stream_count += recovered.source_count;
        details += QCoreApplication::translate(
            "MainWindow.AdxKeyRecovery",
            "%1\nTriplet: %2\nEncryption type: %3\nScore: %4\n"
            "Streams: %5\nFrames per stream: %6\nTotal frames: %7\nEvidence frames: %8\n")
            .arg(target.source)
            .arg(triplet)
            .arg(recovered.encryption_type)
            .arg(score)
            .arg(recovered.source_count)
            .arg(frames_text(recovered.source_frames))
            .arg(recovered.total_frames)
            .arg(recovered.evidence_frames);
        if (task.kind == AdxRecoveryKind::Adx) {
            details += QCoreApplication::translate("MainWindow.AdxKeyRecovery", "Examined frames: %1\n").arg(recovered.examined_frames);
        } else {
            details += QCoreApplication::translate("MainWindow.AdxKeyRecovery", "Component frames: %1,%2,%3\nCandidate counts: %4,%5,%6\n")
                .arg(recovered.component_frames[0])
                .arg(recovered.component_frames[1])
                .arg(recovered.component_frames[2])
                .arg(recovered.candidate_counts[0])
                .arg(recovered.candidate_counts[1])
                .arg(recovered.candidate_counts[2]);
        }
        if (recovered.encryption_type == 9u) {
            details += QCoreApplication::translate("MainWindow.AdxKeyRecovery", "Canonical type-9 keycode: 0x%1\n")
                .arg(QString::number(static_cast<qulonglong>(recovered.canonical_type9_code), 16).toUpper());
        }
        details += QLatin1Char('\n');
        append_log(QCoreApplication::translate(
            "MainWindow.AdxKeyRecovery",
            "%1 key recovery completed for %2: %3, score %4, %n stream(s)",
            nullptr,
            static_cast<int>(recovered.source_count))
            .arg(name, target.source, triplet, score));
    }
    if (!task.errors.empty()) {
        details += QCoreApplication::translate("MainWindow.AdxKeyRecovery", "Unavailable targets\n");
        for (const auto& error : task.errors) {
            details += QStringLiteral("- %1\n").arg(error);
        }
        details += QLatin1Char('\n');
    }
    details += QCoreApplication::translate(
        "MainWindow.AdxKeyRecovery",
        "Candidates were not applied. Any effective triplet that decrypts its streams is valid; scores are "
        "normalized structural agreement, not probability. Exact triplets are grouped and ranked by score, "
        "file support, filename, and key.");

    auto groups = group_key_recovery_candidates(candidates);
    QStringList triplets;
    triplets.reserve(static_cast<qsizetype>(groups.size()));
    for (const auto& group : groups) {
        triplets.push_back(group.key);
    }
    const auto summary = groups.empty()
        ? QCoreApplication::translate("MainWindow.AdxKeyRecovery", "No positive triplet candidate was recovered.")
        : groups.size() == 1
        ? QCoreApplication::translate("MainWindow.AdxKeyRecovery", "Recovered triplet: %1\nScore: %2")
              .arg(groups.front().key)
              .arg(QString::number(groups.front().mean_score, 'f', 6))
        : QCoreApplication::translate("MainWindow.AdxKeyRecovery", "Recovered %1 files in %2 exact-triplet groups.")
              .arg(task.recovered.size())
              .arg(groups.size());
    if (task.requested_sources > 1u) {
        update_key_recovery_progress(
            this, groups, task.requested_sources, task.requested_sources, summary, true);
    } else {
        show_key_recovery_result(
            this,
            QCoreApplication::translate("MainWindow.AdxKeyRecovery", "%1 key recovery").arg(name),
            summary,
            details,
            triplets.join(QLatin1Char('\n')),
            groups.size() == 1 ? QCoreApplication::translate("MainWindow.AdxKeyRecovery", "Copy Triplet") : QCoreApplication::translate("MainWindow.AdxKeyRecovery", "Copy Triplets"),
            std::move(groups)
        );
    }
    statusBar()->showMessage(
        QCoreApplication::translate("MainWindow.AdxKeyRecovery", "%1 key recovery completed. Files: %2. Streams: %3.")
            .arg(name)
            .arg(task.recovered.size())
            .arg(stream_count),
        5000);
}

} // namespace cristudio
