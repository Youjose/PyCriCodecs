#include "../main_window.hpp"

#include "../modules/acb/acb_cue_view.hpp"
#include "../path_text.hpp"
#include "../shared/document_helpers.hpp"
#include "ui_helpers.hpp"

#include <QComboBox>
#include <QCheckBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeView>
#include <QWidget>
#include <QtConcurrentRun>

#include <algorithm>
#include <array>
#include <exception>
#include <map>
#include <ranges>
#include <set>
#include <string_view>
#include <typeinfo>
#include <utility>

namespace cristudio {
namespace {

// Register labels passed through translated_info_row with lupdate.
[[maybe_unused]] constexpr std::array acb_cue_info_sources{
    QT_TRANSLATE_NOOP("MainWindow.AcbCuePreview", "Cue index"),
    QT_TRANSLATE_NOOP("MainWindow.AcbCuePreview", "Cue ID"),
    QT_TRANSLATE_NOOP("MainWindow.AcbCuePreview", "Reference"),
    QT_TRANSLATE_NOOP("MainWindow.AcbCuePreview", "Playable paths"),
    QT_TRANSLATE_NOOP("MainWindow.AcbCuePreview", "Commands"),
    QT_TRANSLATE_NOOP("MainWindow.AcbCuePreview", "Target cue"),
    QT_TRANSLATE_NOOP("MainWindow.AcbCuePreview", "Action chain"),
    QT_TRANSLATE_NOOP("MainWindow.AcbCuePreview", "Selectors"),
    QT_TRANSLATE_NOOP("MainWindow.AcbCuePreview", "Runtime choices"),
    QT_TRANSLATE_NOOP("MainWindow.AcbCuePreview", "Status"),
};

std::string optional_number(const std::optional<uint32_t>& value) {
    return value ? number(*value) : "-";
}

std::string optional_number(const std::optional<uint16_t>& value) {
    return value ? number(*value) : "-";
}

std::string join_strings(
    std::span<const std::string> values,
    std::string_view separator) {
    std::string result;
    for (const auto& value : values) {
        if (!result.empty()) {
            result += separator;
        }
        result += value;
    }
    return result;
}

std::string selector_summary(
    std::span<const modules::acb::CueSelectorValueView> selectors) {
    std::string result;
    for (const auto& selector : selectors) {
        if (!result.empty()) {
            result += ", ";
        }
        result += selector.name + "=" + selector.value;
    }
    return result;
}

std::string choice_summary(
    std::span<const modules::acb::CueChoiceView> choices) {
    std::string result;
    for (const auto& choice : choices) {
        if (!result.empty()) {
            result += ", ";
        }
        result += choice.domain ==
                cricodecs::acb::AcbCueChoiceDomain::sequence_track
            ? "track"
            : "synth";
        result += " " + number(choice.node_index);
        result += ":" + number(choice.occurrence);
        result += "=" + number(choice.option_index);
    }
    return result;
}

bool route_matches_selectors(
    const modules::acb::CueRouteView& route,
    const std::vector<std::pair<std::string, QComboBox*>>& controls) {
    for (const auto& [name, combo] : controls) {
        if (combo == nullptr || combo->currentIndex() < 0) {
            continue;
        }
        const auto selected = combo->currentData().toString().toStdString();
        const auto has_selector = std::ranges::any_of(
            route.selectors,
            [&](const modules::acb::CueSelectorValueView& value) {
                return value.name == name;
            });
        if (!has_selector) {
            continue;
        }
        const auto matches = std::ranges::any_of(
            route.selectors,
            [&](const modules::acb::CueSelectorValueView& value) {
                return value.name == name && value.value == selected;
            });
        if (!matches) {
            return false;
        }
    }
    return true;
}

} // namespace

void MainWindow::show_acb_cue(uint32_t cue_index) {
    if (m_acb_cue_sheet == nullptr ||
        cue_index >= m_acb_cue_sheet->cues.size()) {
        return;
    }

    ++m_preview_request_id;
    m_pending_preview_entry = std::nullopt;
    m_pending_mux_preview = std::nullopt;
    m_pending_acb_cue_preview = false;
    m_current_preview_entry = std::nullopt;
    m_current_acb_cue_index = cue_index;
    m_acb_last_cue_index = cue_index;
    set_preview_entry_actions_visible(true);

    open_preview_panel();
    reset_audio_preview();
    m_preview_key_kind = KeyPanelKind::Cri64;
    update_key_panel(
        m_preview_key_panel,
        m_preview_key_label,
        m_preview_key_input,
        m_preview_key_base_input,
        m_preview_key_apply,
        m_preview_key_kind);

    const auto& cue = m_acb_cue_sheet->cues[cue_index];
    m_nested_title->setText(utf8_to_qstring(cue.name));
    m_nested_subtitle->setText(QCoreApplication::translate(
        "MainWindow.AcbCuePreview",
        "Authored ACB cue"));
    if (m_preview_tabs != nullptr) {
        m_preview_tabs->show();
        m_preview_tabs->setTabEnabled(0, true);
        m_preview_tabs->setTabEnabled(1, false);
        m_preview_tabs->setCurrentIndex(0);
    }
    if (m_nested_image_scroll != nullptr) {
        m_nested_image_scroll->hide();
    }
    if (m_nested_body != nullptr) {
        m_nested_body->hide();
    }

    while (m_acb_cue_selector_form != nullptr &&
           m_acb_cue_selector_form->rowCount() > 1) {
        m_acb_cue_selector_form->removeRow(0);
    }
    m_acb_selector_controls.clear();

    std::map<std::string, std::set<std::string>, std::less<>>
        selector_values;
    for (const auto& route : cue.routes) {
        for (const auto& selector : route.selectors) {
            selector_values[selector.name].insert(selector.value);
        }
    }
    for (const auto& [name, values] : selector_values) {
        if (values.size() < 2) {
            continue;
        }
        auto* combo = new QComboBox(m_acb_cue_controls);
        combo->setObjectName(QStringLiteral("AcbCueSelectorCombo"));
        combo->setAccessibleName(
            QCoreApplication::translate(
                "MainWindow.AcbCuePreview",
                "Cue selector %1")
                .arg(utf8_to_qstring(name)));
        for (const auto& value : values) {
            combo->addItem(utf8_to_qstring(value), utf8_to_qstring(value));
        }
        m_acb_cue_selector_form->insertRow(
            m_acb_cue_selector_form->rowCount() - 1,
            utf8_to_qstring(name),
            combo);
        m_acb_selector_controls.emplace_back(name, combo);
        connect(
            combo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this] {
                refresh_acb_cue_route_choices();
                start_acb_cue_preview();
            });
    }

    m_acb_cue_controls->setVisible(true);
    refresh_acb_cue_route_choices();
    start_acb_cue_preview();
}

void MainWindow::refresh_acb_cue_route_choices() {
    if (m_acb_cue_sheet == nullptr || !m_current_acb_cue_index ||
        *m_current_acb_cue_index >= m_acb_cue_sheet->cues.size() ||
        m_acb_cue_route_combo == nullptr) {
        return;
    }
    const auto& cue =
        m_acb_cue_sheet->cues[*m_current_acb_cue_index];

    const auto prior_route =
        m_acb_cue_route_combo->currentIndex() >= 0
        ? m_acb_cue_route_combo->currentData().toUInt()
        : 0u;
    const QSignalBlocker blocker(m_acb_cue_route_combo);
    m_acb_cue_route_combo->clear();
    for (uint32_t index = 0; index < cue.routes.size(); ++index) {
        const auto& route = cue.routes[index];
        if (!route_matches_selectors(route, m_acb_selector_controls)) {
            continue;
        }
        m_acb_cue_route_combo->addItem(
            utf8_to_qstring(
                modules::acb::cue_route_label(
                    *m_acb_cue_sheet,
                    cue,
                    route,
                    index)),
            index);
    }
    const auto restored = m_acb_cue_route_combo->findData(prior_route);
    if (restored >= 0) {
        m_acb_cue_route_combo->setCurrentIndex(restored);
    }

    const auto route_index =
        m_acb_cue_route_combo->currentIndex() >= 0
        ? std::optional<uint32_t>{
              m_acb_cue_route_combo->currentData().toUInt()}
        : std::nullopt;
    m_acb_cue_route_combo->setVisible(
        m_acb_cue_route_combo->count() > 1 ||
        m_acb_selector_controls.empty());
    if (auto* label =
            m_acb_cue_selector_form->labelForField(
                m_acb_cue_route_combo);
        label != nullptr) {
        label->setVisible(m_acb_cue_route_combo->isVisible());
    }

    std::vector<InfoRow> info{
        translated_info_row(
            "MainWindow.AcbCuePreview",
            "Cue index",
            number(cue.cue_index)),
        translated_info_row(
            "MainWindow.AcbCuePreview",
            "Cue ID",
            number(cue.cue_id)),
        translated_info_row(
            "MainWindow.AcbCuePreview",
            "Reference",
            cue.reference_name + " " + number(cue.reference_index)),
        translated_info_row(
            "MainWindow.AcbCuePreview",
            "Playable paths",
            number(cue.routes.size())),
        translated_info_row(
            "MainWindow.AcbCuePreview",
            "Commands",
            number(cue.commands.size())),
    };

    std::vector<EntrySummary> clip_entries;
    bool has_empty_hold = false;
    if (route_index && *route_index < cue.routes.size()) {
        const auto& route = cue.routes[*route_index];
        if (route.plan_index >= m_acb_cue_sheet->plans.size()) {
            return;
        }
        const auto& plan =
            m_acb_cue_sheet->plans[route.plan_index];
        info.push_back(translated_info_row(
            "MainWindow.AcbCuePreview",
            "Target cue",
            plan.terminal_cue_name + " (" +
                number(plan.terminal_cue_id) + ")"));
        if (!route.action_cue_names.empty()) {
            info.push_back(translated_info_row(
                "MainWindow.AcbCuePreview",
                "Action chain",
                join_strings(route.action_cue_names, " -> ")));
        }
        const auto selectors = selector_summary(route.selectors);
        if (!selectors.empty()) {
            info.push_back(translated_info_row(
                "MainWindow.AcbCuePreview",
                "Selectors",
                selectors));
        }
        const auto choices = choice_summary(route.choices);
        if (!choices.empty()) {
            info.push_back(translated_info_row(
                "MainWindow.AcbCuePreview",
                "Runtime choices",
                choices));
        }

        for (const auto& block : plan.blocks) {
            has_empty_hold = has_empty_hold ||
                (block.authored_loop_count < 0 && block.clips.empty());
            if (block.clips.empty()) {
                EntrySummary entry;
                entry.cells = {
                    block.name,
                    "-",
                    "-",
                    "-",
                    "-",
                    "-",
                    number(block.duration_us) + " us",
                };
                clip_entries.push_back(std::move(entry));
                continue;
            }
            for (const auto& clip : block.clips) {
                EntrySummary entry;
                entry.cells = {
                    block.name,
                    number(clip.waveform_index),
                    optional_number(clip.awb_wave_id),
                    optional_number(clip.awb_stream_index),
                    clip.awb_bank.empty() ? "-" : clip.awb_bank,
                    std::to_string(clip.start_time_us) + " us",
                    number(block.duration_us) + " us",
                };
                clip_entries.push_back(std::move(entry));
            }
        }
    } else if (!cue.status.empty()) {
        info.push_back(translated_info_row(
            "MainWindow.AcbCuePreview",
            "Status",
            cue.status));
    }
    if (m_acb_include_empty_holds != nullptr) {
        if (!has_empty_hold) {
            const QSignalBlocker hold_blocker(m_acb_include_empty_holds);
            m_acb_include_empty_holds->setChecked(false);
        }
        m_acb_include_empty_holds->setVisible(has_empty_hold);
    }
    populate_info_grid(m_nested_info_grid, info);

    m_nested_entry_model->set_entries(
        clip_entries,
        {
            QCoreApplication::translate(
                "MainWindow.AcbCuePreview",
                "Block")
                .toStdString(),
            QCoreApplication::translate(
                "MainWindow.AcbCuePreview",
                "Waveform")
                .toStdString(),
            "AWB ID",
            QCoreApplication::translate(
                "MainWindow.AcbCuePreview",
                "AWB stream")
                .toStdString(),
            QCoreApplication::translate(
                "MainWindow.AcbCuePreview",
                "Bank")
                .toStdString(),
            QCoreApplication::translate(
                "MainWindow.AcbCuePreview",
                "Start")
                .toStdString(),
            QCoreApplication::translate(
                "MainWindow.AcbCuePreview",
                "Duration")
                .toStdString(),
        },
        {"name", "index", "index", "index", "type", "time", "time"});
    m_nested_entry_view->setRootIsDecorated(false);
    m_nested_entry_view->setVisible(!clip_entries.empty());
    if (!clip_entries.empty()) {
        fit_entry_columns(m_nested_entry_view, true);
    }
    m_nested_body->clear();
    m_nested_body->hide();
}

void MainWindow::start_acb_cue_preview() {
    if (m_acb_cue_sheet == nullptr ||
        !m_current_acb_cue_index ||
        *m_current_acb_cue_index >= m_acb_cue_sheet->cues.size() ||
        m_acb_cue_route_combo == nullptr ||
        m_acb_cue_route_combo->currentIndex() < 0) {
        return;
    }
    if (preview_running()) {
        m_pending_acb_cue_preview = true;
        return;
    }
    const auto& cue =
        m_acb_cue_sheet->cues[*m_current_acb_cue_index];
    const auto route_index =
        m_acb_cue_route_combo->currentData().toUInt();
    if (route_index >= cue.routes.size()) {
        return;
    }
    const auto plan_index = cue.routes[route_index].plan_index;
    if (plan_index >= m_acb_cue_sheet->plans.size()) {
        return;
    }

    const auto request_id = ++m_preview_request_id;
    const auto path = m_acb_cue_source_path;
    const auto cue_index = cue.cue_index;
    const auto signature =
        m_acb_cue_sheet->plans[plan_index].semantic_signature;
    const bool include_empty_holds =
        m_acb_include_empty_holds != nullptr &&
        m_acb_include_empty_holds->isChecked();
    auto keys = m_decryption_keys;
    m_pending_acb_cue_preview = false;
    reset_audio_preview();
    show_media_preview_message(QCoreApplication::translate(
        "MainWindow.AcbCuePreview",
        "Rendering cue preview..."));
    m_nested_entry_view->setVisible(
        m_nested_entry_model->rowCount() > 0);
    append_log(
        QCoreApplication::translate(
            "MainWindow.AcbCuePreview",
            "Cue preview started [%1]: %2")
            .arg(request_id)
            .arg(utf8_to_qstring(cue.name)));

    m_preview_watcher->setFuture(QtConcurrent::run(
        [path,
         cue_index,
         signature,
         request_id,
         keys = std::move(keys),
         include_empty_holds] {
            PreviewResult result;
            result.request_id = request_id;
            result.acb_cue_preview = true;
            try {
                auto audio = modules::acb::render_cue_preview(
                    path,
                    cue_index,
                    signature,
                    keys,
                    include_empty_holds);
                if (audio) {
                    result.audio = std::move(*audio);
                } else {
                    result.message = utf8_to_qstring(audio.error());
                }
            } catch (const std::exception& error) {
                result.message = QCoreApplication::translate(
                    "MainWindow.AcbCuePreview",
                    "Cue preview failed: %1 [%2]")
                    .arg(
                        QString::fromLocal8Bit(error.what()),
                        QString::fromLatin1(typeid(error).name()));
            } catch (...) {
                result.message = QCoreApplication::translate(
                    "MainWindow.AcbCuePreview",
                    "Cue preview failed with an unknown exception");
            }
            return result;
        }));
}

} // namespace cristudio
