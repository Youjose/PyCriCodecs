#include "modules/acb/acb_cue_view.hpp"

#include "shared/audio_preview_helpers.hpp"
#include "shared/document_helpers.hpp"
#include "shared/i18n.hpp"

#include "acb_cue_resolver.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <string_view>
#include <utility>

namespace cristudio::modules::acb {
namespace {

// Register strings passed through the UTF-8 translation helper with lupdate.
[[maybe_unused]] constexpr std::array acb_cue_view_sources{
    QT_TRANSLATE_NOOP("Acb.CueView", "Playable"),
    QT_TRANSLATE_NOOP("Acb.CueView", "Multiple paths"),
    QT_TRANSLATE_NOOP("Acb.CueView", "Control / runtime only"),
    QT_TRANSLATE_NOOP("Acb.CueView", "Cue"),
    QT_TRANSLATE_NOOP("Acb.CueView", "Kind"),
    QT_TRANSLATE_NOOP("Acb.CueView", "Paths"),
    QT_TRANSLATE_NOOP("Acb.CueView", "Status"),
    QT_TRANSLATE_NOOP("Acb.CueView", "Plan resolution unavailable"),
    QT_TRANSLATE_NOOP("Acb.CueView", "ACB cue preview load failed: "),
    QT_TRANSLATE_NOOP(
        "Acb.CueView",
        "The selected cue path is no longer available"),
    QT_TRANSLATE_NOOP("Acb.CueView", "ACB rendered cue"),
    QT_TRANSLATE_NOOP("Acb.CueView", "Static cue path preview"),
    QT_TRANSLATE_NOOP("Acb.CueView", "Native codec loop"),
    QT_TRANSLATE_NOOP("Acb.CueView", "option"),
    QT_TRANSLATE_NOOP("Acb.CueView", "Start"),
    QT_TRANSLATE_NOOP("Acb.CueView", "Path"),
};

using cricodecs::acb::AcbCueAwbBank;
using cricodecs::acb::AcbCueChoiceSelection;
using cricodecs::acb::AcbCuePlaybackPlan;
using cricodecs::acb::AcbCuePlanSource;

std::string choice_domain_name(
    cricodecs::acb::AcbCueChoiceDomain domain) {
    switch (domain) {
        case cricodecs::acb::AcbCueChoiceDomain::sequence_track:
            return "sequence track";
        case cricodecs::acb::AcbCueChoiceDomain::synth_reference:
            return "synth reference";
    }
    return "runtime choice";
}

std::string bank_name(const std::optional<AcbCueAwbBank>& bank) {
    if (!bank) {
        return {};
    }
    return *bank == AcbCueAwbBank::stream ? "stream" : "memory";
}

std::string command_table_name(
    cricodecs::acb::AcbCommandTableKind kind) {
    using cricodecs::acb::AcbCommandTableKind;
    switch (kind) {
        case AcbCommandTableKind::track_event:
            return "TrackEvent";
        case AcbCommandTableKind::legacy_command:
            return "Command";
        case AcbCommandTableKind::sequence_command:
            return "SequenceCommand";
        case AcbCommandTableKind::track_command:
            return "TrackCommand";
        case AcbCommandTableKind::synth_command:
            return "SynthCommand";
    }
    return "Command";
}

const cricodecs::acb::AcbCueCommandStream* command_stream_for_node(
    const cricodecs::acb::AcbCueGraph& graph,
    const cricodecs::acb::AcbCueNode& node) {
    using cricodecs::acb::AcbCommandTableKind;
    using cricodecs::acb::AcbCueNodeKind;
    switch (node.kind) {
        case AcbCueNodeKind::track_event:
            return graph.command_stream(
                AcbCommandTableKind::track_event,
                node.index);
        case AcbCueNodeKind::legacy_command:
            return graph.command_stream(
                AcbCommandTableKind::legacy_command,
                node.index);
        case AcbCueNodeKind::sequence_command:
            return graph.command_stream(
                AcbCommandTableKind::sequence_command,
                node.index);
        case AcbCueNodeKind::track_command:
            return graph.command_stream(
                AcbCommandTableKind::track_command,
                node.index);
        case AcbCueNodeKind::synth_command:
            return graph.command_stream(
                AcbCommandTableKind::synth_command,
                node.index);
        default:
            return nullptr;
    }
}

std::vector<CueCommandView> cue_commands(
    const cricodecs::acb::AcbCueGraph& graph,
    uint32_t cue_index) {
    auto assembly = graph.assemble_cue(cue_index);
    if (!assembly) {
        return {};
    }
    std::vector<CueCommandView> result;
    std::set<std::pair<cricodecs::acb::AcbCommandTableKind, uint32_t>>
        seen_streams;
    for (const auto& node : assembly->nodes) {
        const auto* stream = command_stream_for_node(graph, node);
        if (stream == nullptr ||
            !seen_streams.emplace(
                stream->table_kind,
                stream->row_index).second) {
            continue;
        }
        for (uint32_t command_index = 0;
             command_index < stream->commands.size();
             ++command_index) {
            const auto& command = stream->commands[command_index];
            if (command.meaning ==
                cricodecs::acb::AcbCueCommandMeaning::terminator) {
                continue;
            }
            result.push_back({
                .table = command_table_name(stream->table_kind),
                .row_index = stream->row_index,
                .command_index = command_index,
                .code = command.code,
                .meaning = std::string(
                    cricodecs::acb::cue_command_meaning_name(
                        command.meaning)),
                .target_type = command.target
                    ? std::optional<uint16_t>{
                          static_cast<uint16_t>(command.target->type)}
                    : std::nullopt,
                .target_index = command.target
                    ? std::optional<uint16_t>{command.target->index}
                    : std::nullopt,
                .argument_u16 = command.argument_u16,
                .argument_u16_2 = command.argument_u16_2,
                .argument_i16 = command.argument_i16,
            });
        }
    }
    return result;
}

std::vector<CueBlockView> make_blocks(const AcbCuePlaybackPlan& plan) {
    std::vector<CueBlockView> result;
    result.reserve(plan.blocks.size());
    for (const auto& block : plan.blocks) {
        CueBlockView view{
            .name = block.name,
            .duration_us = block.duration_us,
            .authored_loop_count = block.authored_loop_count,
            .render_loop_count = block.render_loop_count,
            .skipped_empty_hold = block.skipped_empty_hold,
            .clips = {},
        };
        view.clips.reserve(block.clips.size());
        for (const auto& clip : block.clips) {
            view.clips.push_back({
                .waveform_index = clip.waveform_index,
                .start_time_us = clip.start_time_us,
                .awb_wave_id = clip.awb_wave_id,
                .awb_stream_index = clip.awb_stream_index,
                .awb_bank = bank_name(clip.awb_bank),
                .awb_port_no = clip.awb_port_no,
            });
        }
        result.push_back(std::move(view));
    }
    return result;
}

void add_selector(
    std::vector<CueSelectorValueView>& selectors,
    std::string_view name,
    std::string_view value) {
    if (name.empty()) {
        return;
    }
    const auto duplicate = std::ranges::any_of(
        selectors,
        [name, value](const CueSelectorValueView& current) {
            return current.name == name && current.value == value;
        });
    if (!duplicate) {
        selectors.push_back({
            .name = std::string(name),
            .value = std::string(value),
        });
    }
}

CueRouteView make_route(
    const cricodecs::acb::AcbCueGraph& graph,
    uint32_t plan_index,
    const AcbCuePlanSource& source,
    std::span<const AcbCueChoiceSelection> choices) {
    CueRouteView route{
        .plan_index = plan_index,
        .action_cue_chain = source.action_cue_chain,
        .action_cue_names = {},
        .selectors = {},
        .choices = {},
    };
    route.action_cue_names.reserve(route.action_cue_chain.size());
    for (const auto cue_index : route.action_cue_chain) {
        route.action_cue_names.push_back(
            cue_index < graph.cues().size()
                ? std::string(graph.cue_name(cue_index))
                : "cue_" + std::to_string(cue_index));
    }
    for (const auto& selector : source.selector_values) {
        add_selector(route.selectors, selector.name, selector.value);
    }
    route.choices.reserve(choices.size());
    for (const auto& choice : choices) {
        route.choices.push_back({
            .domain = choice.domain,
            .node_index = choice.node_index,
            .occurrence = choice.occurrence,
            .option_index = choice.option_index,
            .mode = choice.mode,
            .selector_name = choice.selector_name,
            .selector_value = choice.selector_value,
        });
        add_selector(
            route.selectors,
            choice.selector_name,
            choice.selector_value);
    }
    return route;
}

std::string route_status(const CueView& cue) {
    if (!cue.routes.empty()) {
        return cue.routes.size() == 1
            ? cristudio::i18n::translate_utf8("Acb.CueView", "Playable")
            : cristudio::i18n::translate_utf8("Acb.CueView", "Multiple paths");
    }
    return cue.status.empty()
        ? cristudio::i18n::translate_utf8("Acb.CueView", "Control / runtime only")
        : cue.status;
}

} // namespace

CueSheetView build_cue_sheet_view(
    const std::filesystem::path& path,
    const cricodecs::acb::AcbContainer& acb) {
    const auto& graph = acb.cue_graph();
    CueSheetView view;
    view.entry_columns = {
        cristudio::i18n::translate_utf8("Acb.CueView", "Cue"),
        cristudio::i18n::translate_utf8("Acb.CueView", "Kind"),
        cristudio::i18n::translate_utf8("Acb.CueView", "Paths"),
        cristudio::i18n::translate_utf8("Acb.CueView", "Status"),
    };
    view.entry_column_types = {"name", "type", "count", "status"};
    view.cues.reserve(graph.cues().size());

    for (uint32_t index = 0; index < graph.cues().size(); ++index) {
        const auto& cue = graph.cues()[index];
        auto name = std::string(graph.cue_name(index));
        if (name.empty()) {
            name = "cue_" + std::to_string(index);
        }
        view.cues.push_back({
            .cue_index = index,
            .cue_id = cue.cue_id,
            .reference_type = cue.reference.type,
            .reference_index = cue.reference.index,
            .name = std::move(name),
            .reference_name =
                std::string(cricodecs::acb::reference_type_name(
                    cue.reference.type)),
            .routes = {},
            .commands = cue_commands(graph, index),
            .status = {},
        });
    }

    auto resolution = cricodecs::acb::resolve_cue_sheet_playback(acb);
    if (resolution) {
        const auto output_names =
            cricodecs::acb::cue_plan_filenames(*resolution);
        view.plans.reserve(resolution->plans.size());
        for (size_t resolved_index = 0;
             resolved_index < resolution->plans.size();
             ++resolved_index) {
            const auto& resolved = resolution->plans[resolved_index];
            const auto plan_index =
                static_cast<uint32_t>(view.plans.size());
            view.plans.push_back({
                .semantic_signature =
                    cricodecs::acb::cue_plan_semantic_signature(
                        graph,
                        resolved.plan),
                .output_name = output_names[resolved_index],
                .terminal_cue_index = resolved.plan.cue_index,
                .terminal_cue_id = resolved.plan.cue_id,
                .terminal_cue_name = resolved.plan.cue_name,
                .blocks = make_blocks(resolved.plan),
            });
            for (const auto& source : resolved.sources) {
                if (source.source_cue_index >= view.cues.size()) {
                    continue;
                }
                auto& cue = view.cues[source.source_cue_index];
                if (source.paths.empty()) {
                    cue.routes.push_back(
                        make_route(graph, plan_index, source, {}));
                    continue;
                }
                for (const auto& choices : source.paths) {
                    cue.routes.push_back(make_route(
                        graph,
                        plan_index,
                        source,
                        choices));
                }
            }
        }
        for (const auto cue_index : resolution->non_playable_cues) {
            if (cue_index < view.cues.size()) {
                view.cues[cue_index].status =
                    cristudio::i18n::translate_utf8(
                        "Acb.CueView",
                        "Control / runtime only");
            }
        }
    } else {
        for (auto& cue : view.cues) {
            cue.status = cristudio::i18n::translate_utf8(
                "Acb.CueView",
                "Plan resolution unavailable");
        }
    }

    view.entries.reserve(view.cues.size());
    for (const auto& cue : view.cues) {
        EntrySummary entry;
        entry.name = cue.name;
        entry.type = cue.reference_name;
        entry.size = number(cue.routes.size());
        entry.detail = route_status(cue);
        entry.cells = {
            cue.name,
            cue.reference_name,
            number(cue.routes.size()),
            entry.detail,
        };
        entry.source_path = path;
        entry.source_format = "ACB Cue";
        entry.source_index = cue.cue_index;
        view.entries.push_back(std::move(entry));
    }
    return view;
}

std::expected<AudioPreview, std::string> render_cue_preview(
    const std::filesystem::path& path,
    uint32_t source_cue_index,
    std::string_view semantic_signature,
    const DecryptionKeys& keys,
    bool include_empty_holds) {
    auto acb = cricodecs::acb::AcbContainer::load(path);
    if (!acb) {
        return std::unexpected(
            cristudio::i18n::translate_utf8(
                "Acb.CueView",
                "ACB cue preview load failed: ") +
            acb.error());
    }
    auto resolution =
        cricodecs::acb::resolve_cue_playback_paths(*acb, source_cue_index);
    if (!resolution) {
        return std::unexpected(resolution.error());
    }
    const auto& graph = acb->cue_graph();
    const auto selected = std::ranges::find_if(
        resolution->plans,
        [&](const cricodecs::acb::AcbResolvedCuePlan& candidate) {
            return cricodecs::acb::cue_plan_semantic_signature(
                       graph,
                       candidate.plan) == semantic_signature;
        });
    if (selected == resolution->plans.end()) {
        return std::unexpected(
            cristudio::i18n::translate_utf8(
                "Acb.CueView",
                "The selected cue path is no longer available"));
    }

    auto plan = selected->plan;
    for (auto& block : plan.blocks) {
        if (block.authored_loop_count < 0 && block.clips.empty()) {
            block.skipped_empty_hold = !include_empty_holds;
            block.render_loop_count = 0;
        }
    }
    cricodecs::acb::AcbCueRenderOptions options{
        .include_empty_infinite_blocks = include_empty_holds,
        .hca_keycode = keys.has_cri_key ? keys.cri_key : 0,
        .hca_subkey = keys.hca_subkey == 0
            ? std::nullopt
            : std::optional<uint16_t>{keys.hca_subkey},
    };
    auto rendered = cricodecs::acb::render_cue_plan(
        *acb,
        std::move(plan),
        options);
    if (!rendered) {
        return std::unexpected(rendered.error());
    }
    auto wav = cricodecs::acb::build_rendered_cue_wav(*rendered);
    if (!wav) {
        return std::unexpected(wav.error());
    }
    const auto sample_count = rendered->channels == 0
        ? uint64_t{0}
        : static_cast<uint64_t>(
              rendered->pcm.size() / rendered->channels);
    std::vector<AudioLoop> loops;
    loops.reserve(rendered->loops.size());
    for (const auto& loop : rendered->loops) {
        if (loop.plan_block_index >= rendered->plan.blocks.size()) {
            continue;
        }
        const auto& block = rendered->plan.blocks[loop.plan_block_index];
        if (loop.end_sample <= loop.start_sample ||
            loop.end_sample > sample_count) {
            continue;
        }
        auto name = block.name;
        if (loop.kind ==
            cricodecs::acb::AcbRenderedLoopKind::native_waveform) {
            name = cristudio::i18n::translate_utf8(
                "Acb.CueView",
                "Native codec loop");
            if (!block.name.empty()) {
                name += " - " + block.name;
            }
        }
        loops.push_back({
            .name = std::move(name),
            .start_sample = loop.start_sample,
            .end_sample = loop.end_sample,
        });
    }
    return make_wav_audio_preview(
        std::move(*wav),
        rendered->sample_rate,
        rendered->channels,
        sample_count,
        cristudio::i18n::translate_utf8(
            "Acb.CueView",
            "ACB rendered cue"),
        cristudio::i18n::translate_utf8(
            "Acb.CueView",
            "Static cue path preview"),
        std::move(loops));
}

std::string cue_route_label(
    const CueSheetView& sheet,
    const CueView& cue,
    const CueRouteView& route,
    size_t route_index) {
    std::string result;
    for (const auto& selector : route.selectors) {
        if (!result.empty()) {
            result += ", ";
        }
        result += selector.name + "=" + selector.value;
    }
    if (result.empty() && !route.choices.empty()) {
        const auto& choice = route.choices.front();
        result =
            choice_domain_name(choice.domain) + " " +
            std::to_string(choice.node_index) + ":" +
            std::to_string(choice.occurrence) + " " +
            cristudio::i18n::translate_utf8("Acb.CueView", "option") +
            " " +
            std::to_string(choice.option_index);
    }
    const auto* plan = route.plan_index < sheet.plans.size()
        ? &sheet.plans[route.plan_index]
        : nullptr;
    if (result.empty() && plan != nullptr &&
        plan->terminal_cue_name != cue.name) {
        result =
            cristudio::i18n::translate_utf8("Acb.CueView", "Start") +
            " " + plan->terminal_cue_name;
    }
    if (result.empty()) {
        result =
            cristudio::i18n::translate_utf8("Acb.CueView", "Path") +
            " " + std::to_string(route_index + 1);
    }
    return result;
}

} // namespace cristudio::modules::acb
