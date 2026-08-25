/**
 * @file acb_cue_renderer.cpp
 * @brief Static ACB cue planning, timed layering, and supported audio decoding.
 */

#include "acb_cue_renderer.hpp"

#include "../adx/adx_codec.hpp"
#include "../hca/hca_codec.hpp"
#include "../wav/wav_container.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace cricodecs::acb {

namespace {

struct ReferenceKey {
    uint16_t type = 0;
    uint16_t index = invalid_acb_index;
    friend bool operator<(const ReferenceKey& lhs, const ReferenceKey& rhs) noexcept {
        return std::tie(lhs.type, lhs.index) < std::tie(rhs.type, rhs.index);
    }
};

struct AuthoredAwbReference {
    uint16_t wave_id = invalid_acb_index;
    AcbCueAwbBank bank = AcbCueAwbBank::memory;
};

struct PendingChoice {
    AcbCueChoiceDomain domain = AcbCueChoiceDomain::sequence_track;
    uint32_t node_index = 0;
    uint32_t occurrence = 0;
    uint32_t option_count = 0;
    uint8_t mode = 0;
    std::vector<std::pair<std::string, std::string>> selector_labels;
};

std::optional<AuthoredAwbReference> authored_awb_reference(
    const AcbCueWaveform& waveform) {
    const bool stream_bank = waveform.streaming != 0;
    const auto first = stream_bank
        ? waveform.stream_awb_id
        : waveform.memory_awb_id;
    const auto fallback = stream_bank
        ? waveform.memory_awb_id
        : waveform.stream_awb_id;
    const auto wave_id = first != invalid_acb_index
        ? first
        : waveform.id != invalid_acb_index
            ? waveform.id
            : fallback;
    if (wave_id == invalid_acb_index) {
        return std::nullopt;
    }
    return AuthoredAwbReference{
        .wave_id = wave_id,
        .bank = stream_bank
            ? AcbCueAwbBank::stream
            : AcbCueAwbBank::memory,
    };
}

class PlaybackPlanner {
public:
    PlaybackPlanner(
        const AcbCueGraph& graph,
        uint32_t cue_index,
        const AcbCueRenderOptions& options,
        std::span<const AcbCueChoiceSelection> choices = {})
        : m_graph(graph),
          m_cue_index(cue_index),
          m_options(options),
          m_choices(choices) {}

    [[nodiscard]] const std::optional<PendingChoice>& pending_choice() const noexcept {
        return m_pending_choice;
    }

    std::expected<AcbCuePlaybackPlan, std::string> build() {
        std::set<std::tuple<AcbCueChoiceDomain, uint32_t, uint32_t>> choice_keys;
        for (const auto& choice : m_choices) {
            const auto key = std::tuple{
                choice.domain, choice.node_index, choice.occurrence};
            if (!choice_keys.insert(key).second) {
                return std::unexpected(
                    "ACB cue plan failed: duplicate runtime choice selection");
            }
        }
        std::set<uint32_t> override_positions;
        for (const auto& override : m_options.block_loop_overrides) {
            if (!override_positions.insert(override.block_position).second) {
                return std::unexpected(
                    "ACB cue plan failed: duplicate loop override for block position " +
                    std::to_string(override.block_position));
            }
        }
        if (m_cue_index >= m_graph.cues().size()) {
            return std::unexpected("ACB cue plan failed: cue index is out of range");
        }

        const auto& cue = m_graph.cues()[m_cue_index];
        m_plan.cue_index = m_cue_index;
        m_plan.cue_id = cue.cue_id;
        m_plan.cue_name = std::string(m_graph.cue_name(m_cue_index));
        if (m_plan.cue_name.empty()) {
            m_plan.cue_name = "cue_" + std::to_string(m_cue_index);
        }

        if (cue.reference.type == 8 || cue.reference.type == 9) {
            auto result = append_block_sequence(cue.reference.index);
            if (!result) return std::unexpected(result.error());
        } else {
            if (!m_options.block_loop_overrides.empty()) {
                return std::unexpected(
                    "ACB cue plan failed: block loop overrides require a BlockSequence cue");
            }
            AcbCueBlockPlan block{
                .block_position = std::nullopt,
                .block_index = std::nullopt,
                .name = m_plan.cue_name,
                .clips = {},
            };
            auto result = append_reference(
                cue.reference.type, cue.reference.index, 0, block.clips);
            if (!result) return std::unexpected(result.error());
            block.duration_us = inferred_duration_us(block.clips);
            if (!block.clips.empty()) {
                m_plan.blocks.push_back(std::move(block));
            }
        }

        if (std::ranges::none_of(m_plan.blocks, [](const AcbCueBlockPlan& block) {
                return !block.skipped_empty_hold && !block.clips.empty();
            })) {
            return std::unexpected(
                "ACB cue plan failed: cue `" + m_plan.cue_name +
                "` has no statically playable audio");
        }
        if (m_used_choices.size() != m_choices.size()) {
            return std::unexpected(
                "ACB cue plan failed: runtime choice does not belong to the selected path");
        }
        return std::move(m_plan);
    }

private:
    std::expected<void, std::string> append_block_sequence(uint16_t index) {
        if (index >= m_graph.block_sequences().size()) {
            return std::unexpected(
                "ACB cue plan failed: block-sequence index is out of range");
        }
        const auto& sequence = m_graph.block_sequences()[index];
        // Individual Block.TrackIndex lists schedule audio. Top-level
        // BlockSequence tracks carry sequence parameters in Music.acb and do
        // not have TrackEvent rows; those parameters remain inspectable.
        if (!sequence.track_indices.empty()) {
            m_plan.diagnostics.push_back(
                "top-level BlockSequence track parameters are preserved by the "
                "graph but are not applied by the static renderer");
        }
        if (sequence.num_watch_actions != 0 || sequence.num_stop_actions != 0) {
            m_plan.diagnostics.push_back(
                "watch/stop action tracks are preserved by the graph but are not executed "
                "by the static renderer");
        }

        for (const auto& override : m_options.block_loop_overrides) {
            if (override.block_position >= sequence.block_indices.size()) {
                return std::unexpected(
                    "ACB cue plan failed: block loop override position " +
                    std::to_string(override.block_position) + " is out of range");
            }
        }

        for (size_t position = 0;
             position < sequence.block_indices.size();
             ++position) {
            const auto block_position = static_cast<uint32_t>(position);
            const auto block_index = sequence.block_indices[position];
            if (block_index >= m_graph.blocks().size()) {
                return std::unexpected(
                    "ACB cue plan failed: block index is out of range");
            }
            const auto& authored = m_graph.blocks()[block_index];
            const int32_t loop_count = static_cast<int16_t>(authored.num_loops);
            AcbCueBlockPlan block{
                .block_position = block_position,
                .block_index = block_index,
                .name = std::string(m_graph.string_value(authored.name_index)),
                .duration_us = authored.duration_us(),
                .authored_loop_count = loop_count,
                .render_loop_count = loop_count < 0
                    ? m_options.infinite_block_loop_count
                    : static_cast<uint32_t>(loop_count),
                .clips = {},
            };
            if (block.name.empty()) {
                block.name = "block_" + std::to_string(block_index);
            }

            for (const auto track_index : authored.track_indices) {
                auto result = append_track(track_index, 0, block.clips);
                if (!result) return result;
            }
            if (authored.num_action_tracks != 0) {
                m_plan.diagnostics.push_back(
                    "block `" + block.name +
                    "` has action tracks that are not executed by the static renderer");
            }

            const auto override = std::ranges::find_if(
                m_options.block_loop_overrides,
                [block_position](const AcbCueBlockLoopOverride& candidate) {
                    return candidate.block_position == block_position;
                });
            const bool has_override =
                override != m_options.block_loop_overrides.end();
            if (has_override) {
                block.render_loop_count = override->loop_count;
                m_plan.diagnostics.push_back(
                    "block `" + block.name + "` at position " +
                    std::to_string(block_position) + " repeats " +
                    std::to_string(block.render_loop_count) +
                    " time(s) by explicit override");
            }

            if (loop_count < 0) {
                const bool empty_hold = block.clips.empty();
                block.forced_advance =
                    !empty_hold && m_options.advance_after_infinite_block;
                if (empty_hold &&
                    !m_options.include_empty_infinite_blocks &&
                    !has_override) {
                    block.render_loop_count = 0;
                    block.skipped_empty_hold = true;
                    m_plan.diagnostics.push_back(
                        "block `" + block.name +
                        "` is an authored infinite hold with no scheduled waveform; "
                        "the static render skips its synthesized duration");
                } else {
                    if (empty_hold && !has_override) {
                        block.render_loop_count = 0;
                    }
                    m_plan.diagnostics.push_back(
                        "block `" + block.name +
                        "` is authored with LoopNum=-1 (infinite); "
                        "the static render repeats it " +
                        std::to_string(block.render_loop_count) + " time(s)" +
                        (block.forced_advance
                             ? " and then forces the next authored block"
                             : empty_hold
                                 ? " without applying the global loop/stop policy"
                                 : " and stops there"));
                }
            }
            m_plan.blocks.push_back(std::move(block));
            if (loop_count < 0 &&
                !m_options.advance_after_infinite_block &&
                !m_plan.blocks.back().clips.empty()) {
                break;
            }
        }
        return {};
    }

    std::expected<void, std::string> append_track(
        uint16_t index,
        int64_t base_time_us,
        std::vector<AcbCueClipPlan>& clips) {
        if (index >= m_graph.tracks().size()) {
            return std::unexpected("ACB cue plan failed: track index is out of range");
        }
        const auto& track = m_graph.tracks()[index];
        const auto kind = m_graph.track_events().empty()
            ? AcbCommandTableKind::legacy_command
            : AcbCommandTableKind::track_event;
        const auto* stream = m_graph.command_stream(kind, track.event_index);
        if (stream == nullptr) {
            return {};
        }
        for (const auto& target : stream->scheduled_targets) {
            auto result = append_reference(
                static_cast<uint16_t>(target.target.type),
                target.target.index,
                base_time_us + target.time_us,
                clips);
            if (!result) return result;
        }
        return {};
    }

    std::expected<void, std::string> append_reference(
        uint16_t type,
        uint16_t index,
        int64_t start_time_us,
        std::vector<AcbCueClipPlan>& clips) {
        if (start_time_us < 0) {
            return std::unexpected(
                "ACB cue plan failed: negative scheduled waveform time is unsupported");
        }
        const ReferenceKey key{type, index};
        if (m_active.size() >= 64 || !m_active.insert(key).second) {
            return std::unexpected(
                "ACB cue plan failed: cyclic or excessively deep playback reference");
        }

        std::expected<void, std::string> result;
        switch (type) {
            case 0:
                break;
            case 1:
                if (index >= m_graph.waveforms().size()) {
                    result = std::unexpected(
                        "ACB cue plan failed: waveform index is out of range");
                } else {
                    AcbCueClipPlan clip{
                        .waveform_index = index,
                        .start_time_us = start_time_us,
                        .awb_wave_id = std::nullopt,
                        .awb_stream_index = std::nullopt,
                        .awb_bank = std::nullopt,
                    };
                    if (const auto awb = authored_awb_reference(
                            m_graph.waveforms()[index])) {
                        clip.awb_wave_id = awb->wave_id;
                        clip.awb_bank = awb->bank;
                    }
                    clips.push_back(std::move(clip));
                }
                break;
            case 2:
            case 6:
                result = append_synth(index, start_time_us, clips);
                break;
            case 3:
            case 7:
                result = append_sequence(index, start_time_us, clips);
                break;
            case 5:
                result = std::unexpected(
                    "ACB cue plan failed: outside-link cues require the linked ACB");
                break;
            case 8:
            case 9:
                result = std::unexpected(
                    "ACB cue plan failed: nested block sequences are not supported");
                break;
            default:
                result = std::unexpected(
                    "ACB cue plan failed: unsupported reference type " +
                    std::to_string(type));
                break;
        }
        m_active.erase(key);
        return result;
    }

    std::expected<void, std::string> append_synth(
        uint16_t index,
        int64_t start_time_us,
        std::vector<AcbCueClipPlan>& clips) {
        if (index >= m_graph.synths().size()) {
            return std::unexpected("ACB cue plan failed: synth index is out of range");
        }
        const auto& synth = m_graph.synths()[index];
        if (synth.reference_items.size() > 1 && synth.type != 0) {
            const auto choice = selected_option(
                AcbCueChoiceDomain::synth_reference,
                index,
                synth.type,
                static_cast<uint32_t>(synth.reference_items.size()),
                {});
            if (!choice) {
                if (m_choice_error) {
                    return std::unexpected(*m_choice_error);
                }
                return std::unexpected(
                    "ACB cue plan failed: synth type `" +
                    std::string(sequence_type_name(synth.type)) +
                    "` requires a runtime choice");
            }
            const auto& reference = synth.reference_items[*choice];
            return append_reference(
                reference.type, reference.index, start_time_us, clips);
        }
        for (const auto& reference : synth.reference_items) {
            auto result = append_reference(
                reference.type, reference.index, start_time_us, clips);
            if (!result) return result;
        }
        return {};
    }

    std::expected<void, std::string> append_sequence(
        uint16_t index,
        int64_t start_time_us,
        std::vector<AcbCueClipPlan>& clips) {
        if (index >= m_graph.sequences().size()) {
            return std::unexpected("ACB cue plan failed: sequence index is out of range");
        }
        const auto& sequence = m_graph.sequences()[index];
        if (sequence.track_indices.size() > 1 && sequence.type != 0) {
            std::vector<std::pair<std::string, std::string>> selector_labels(
                sequence.track_indices.size());
            for (size_t ordinal = 0;
                 ordinal < sequence.track_indices.size();
                 ++ordinal) {
                const auto track_index = sequence.track_indices[ordinal];
                if (track_index >= m_graph.tracks().size()) {
                    continue;
                }
                const auto& track = m_graph.tracks()[track_index];
                const auto* commands = m_graph.command_stream(
                    AcbCommandTableKind::track_command, track.command_index);
                if (commands == nullptr) {
                    continue;
                }
                for (const auto& command : commands->commands) {
                    if (command.meaning != AcbCueCommandMeaning::selector_condition ||
                        !command.argument_u16 ||
                        !command.argument_u16_2) {
                        continue;
                    }
                    selector_labels[ordinal] = {
                        std::string(m_graph.string_value(*command.argument_u16)),
                        std::string(m_graph.string_value(*command.argument_u16_2)),
                    };
                    break;
                }
            }
            const auto choice = selected_option(
                AcbCueChoiceDomain::sequence_track,
                index,
                sequence.type,
                static_cast<uint32_t>(sequence.track_indices.size()),
                std::move(selector_labels));
            if (!choice) {
                if (m_choice_error) {
                    return std::unexpected(*m_choice_error);
                }
                return std::unexpected(
                    "ACB cue plan failed: sequence type `" +
                    std::string(sequence_type_name(sequence.type)) +
                    "` requires a runtime track choice");
            }
            return append_track(
                sequence.track_indices[*choice], start_time_us, clips);
        }
        if (sequence.num_action_tracks != 0 ||
            sequence.num_watch_actions != 0 ||
            sequence.num_stop_actions != 0) {
            m_plan.diagnostics.push_back(
                "cue sequence action tracks are preserved by the graph but are not "
                "executed by the static renderer");
        }
        for (const auto track_index : sequence.track_indices) {
            auto result = append_track(track_index, start_time_us, clips);
            if (!result) return result;
        }
        return {};
    }

    std::optional<uint32_t> selected_option(
        AcbCueChoiceDomain domain,
        uint32_t node_index,
        uint8_t mode,
        uint32_t option_count,
        std::vector<std::pair<std::string, std::string>> selector_labels) {
        const auto occurrence_key = std::pair{domain, node_index};
        const uint32_t occurrence = m_choice_occurrences[occurrence_key]++;
        const auto selected = std::ranges::find_if(
            m_choices,
            [=](const AcbCueChoiceSelection& choice) {
                return choice.domain == domain &&
                    choice.node_index == node_index &&
                    choice.occurrence == occurrence;
            });
        if (selected != m_choices.end()) {
            if (selected->option_index >= option_count) {
                m_choice_error =
                    "ACB cue plan failed: runtime choice option is out of range";
                return std::nullopt;
            }
            if (selected->mode != mode) {
                m_choice_error =
                    "ACB cue plan failed: runtime choice mode does not match the authored node";
                return std::nullopt;
            }
            if (selected->option_index < selector_labels.size()) {
                const auto& label = selector_labels[selected->option_index];
                if ((!selected->selector_name.empty() &&
                     selected->selector_name != label.first) ||
                    (!selected->selector_value.empty() &&
                     selected->selector_value != label.second)) {
                    m_choice_error =
                        "ACB cue plan failed: runtime selector label does not match the authored option";
                    return std::nullopt;
                }
            }
            m_used_choices.insert(std::tuple{
                domain, node_index, occurrence});
            return selected->option_index;
        }
        m_pending_choice = PendingChoice{
            .domain = domain,
            .node_index = node_index,
            .occurrence = occurrence,
            .option_count = option_count,
            .mode = mode,
            .selector_labels = std::move(selector_labels),
        };
        return std::nullopt;
    }

    uint64_t inferred_duration_us(std::span<const AcbCueClipPlan> clips) const {
        uint64_t duration = 0;
        for (const auto& clip : clips) {
            if (clip.waveform_index >= m_graph.waveforms().size()) continue;
            const auto& waveform = m_graph.waveforms()[clip.waveform_index];
            if (waveform.sampling_rate == 0 || clip.start_time_us < 0) continue;
            const uint64_t audio_us =
                (static_cast<uint64_t>(waveform.num_samples) * 1'000'000 +
                 waveform.sampling_rate - 1) /
                waveform.sampling_rate;
            duration = std::max(
                duration, static_cast<uint64_t>(clip.start_time_us) + audio_us);
        }
        return duration;
    }

    const AcbCueGraph& m_graph;
    uint32_t m_cue_index;
    const AcbCueRenderOptions& m_options;
    AcbCuePlaybackPlan m_plan;
    std::set<ReferenceKey> m_active;
    std::span<const AcbCueChoiceSelection> m_choices;
    std::map<std::pair<AcbCueChoiceDomain, uint32_t>, uint32_t> m_choice_occurrences;
    std::optional<PendingChoice> m_pending_choice;
    std::set<std::tuple<AcbCueChoiceDomain, uint32_t, uint32_t>> m_used_choices;
    std::optional<std::string> m_choice_error;
};

struct DecodedWaveform {
    uint32_t sample_rate = 0;
    uint8_t channels = 0;
    std::vector<int16_t> pcm;
};

struct DecodedSourceKey {
    uint32_t index = 0;
    awb::EntryCodec codec = awb::EntryCodec::Unknown;
    AcbCueAwbBank bank = AcbCueAwbBank::memory;
    bool physical_awb_entry = false;

    friend bool operator<(
        const DecodedSourceKey& lhs,
        const DecodedSourceKey& rhs) noexcept {
        return std::tie(
            lhs.physical_awb_entry,
            lhs.bank,
            lhs.index,
            lhs.codec) <
            std::tie(
                rhs.physical_awb_entry,
                rhs.bank,
                rhs.index,
                rhs.codec);
    }
};

uint64_t frames_for_us(uint64_t time_us, uint32_t sample_rate) {
    return (time_us * sample_rate + 500'000) / 1'000'000;
}

int16_t saturate_sample(int64_t sample) noexcept {
    return static_cast<int16_t>(std::clamp<int64_t>(
        sample,
        std::numeric_limits<int16_t>::min(),
        std::numeric_limits<int16_t>::max()));
}

std::string safe_cue_name(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (const unsigned char ch : name) {
        if (ch < 0x20 || ch == '/' || ch == '\\' || ch == ':' || ch == '*' ||
            ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            result.push_back('_');
        } else {
            result.push_back(static_cast<char>(ch));
        }
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '.')) {
        result.pop_back();
    }
    return result.empty() ? "cue" : result;
}

std::string semantic_plan_signature(
    const AcbCueGraph& graph,
    const AcbCuePlaybackPlan& plan) {
    std::ostringstream out;
    for (const auto& block : plan.blocks) {
        out << "B:"
            << block.duration_us << ':'
            << block.authored_loop_count << ':'
            << block.render_loop_count << ':'
            << block.skipped_empty_hold << ';';
        for (const auto& clip : block.clips) {
            out << "C:"
                << clip.start_time_us << ':'
                << clip.awb_wave_id.value_or(invalid_acb_index) << ':'
                << static_cast<unsigned>(clip.awb_bank.value_or(
                    AcbCueAwbBank::memory));
            if (clip.waveform_index < graph.waveforms().size()) {
                const auto& waveform = graph.waveforms()[clip.waveform_index];
                out << ':'
                    << static_cast<unsigned>(waveform.encode_type) << ':'
                    << waveform.sampling_rate << ':'
                    << waveform.num_samples << ':'
                    << static_cast<unsigned>(waveform.loop_flag) << ':'
                    << waveform.stream_awb_port_no;
                if (waveform.extension_data < graph.waveform_extensions().size()) {
                    const auto& extension =
                        graph.waveform_extensions()[waveform.extension_data];
                    out << ':' << extension.loop_start << ':' << extension.loop_end;
                }
            }
            out << ';';
        }
    }
    return std::move(out).str();
}

} // namespace

std::expected<AcbCuePlaybackPlan, std::string> plan_cue_playback(
    const AcbCueGraph& graph,
    uint32_t cue_index,
    const AcbCueRenderOptions& options) {
    return PlaybackPlanner(graph, cue_index, options).build();
}

std::expected<AcbCuePlaybackPlan, std::string> plan_cue_playback(
    const AcbCueGraph& graph,
    uint32_t cue_index,
    std::span<const AcbCueChoiceSelection> choices,
    const AcbCueRenderOptions& options) {
    return PlaybackPlanner(graph, cue_index, options, choices).build();
}

std::expected<AcbCuePlanEnumeration, std::string> enumerate_cue_playback(
    const AcbCueGraph& graph,
    uint32_t cue_index,
    const AcbCueEnumerationOptions& options) {
    if (options.max_paths == 0) {
        return std::unexpected(
            "ACB cue enumeration failed: max_paths must be greater than zero");
    }
    if (cue_index >= graph.cues().size()) {
        return std::unexpected(
            "ACB cue enumeration failed: cue index is out of range");
    }

    AcbCuePlanEnumeration result{
        .cue_index = cue_index,
        .variants = {},
        .terminal_errors = {},
        .terminal_paths = {},
        .explored_paths = 0,
    };
    std::vector<std::vector<AcbCueChoiceSelection>> pending_paths(1);
    std::map<std::string, size_t> signature_to_variant;
    std::set<std::string> terminal_errors;

    for (size_t position = 0; position < pending_paths.size(); ++position) {
        if (++result.explored_paths > options.max_paths) {
            return std::unexpected(
                "ACB cue enumeration failed: path limit " +
                std::to_string(options.max_paths) + " exceeded");
        }

        auto choices = std::move(pending_paths[position]);
        PlaybackPlanner planner(graph, cue_index, options.render, choices);
        auto plan = planner.build();
        if (plan) {
            const auto signature = semantic_plan_signature(graph, *plan);
            const auto [it, inserted] =
                signature_to_variant.emplace(signature, result.variants.size());
            if (inserted) {
                result.variants.push_back({
                    .plan = std::move(*plan),
                    .paths = {std::move(choices)},
                });
            } else {
                result.variants[it->second].paths.push_back(std::move(choices));
            }
            continue;
        }

        const auto& request = planner.pending_choice();
        if (!request) {
            terminal_errors.insert(plan.error());
            result.terminal_paths.push_back({
                .choices = std::move(choices),
                .error = plan.error(),
            });
            continue;
        }
        if (pending_paths.size() + request->option_count > options.max_paths) {
            return std::unexpected(
                "ACB cue enumeration failed: path limit " +
                std::to_string(options.max_paths) + " exceeded");
        }
        for (uint32_t option = 0; option < request->option_count; ++option) {
            auto branch = choices;
            AcbCueChoiceSelection selection{
                .domain = request->domain,
                .node_index = request->node_index,
                .occurrence = request->occurrence,
                .option_index = option,
                .mode = request->mode,
                .selector_name = {},
                .selector_value = {},
            };
            if (option < request->selector_labels.size()) {
                selection.selector_name =
                    request->selector_labels[option].first;
                selection.selector_value =
                    request->selector_labels[option].second;
            }
            branch.push_back(std::move(selection));
            pending_paths.push_back(std::move(branch));
        }
    }

    result.terminal_errors.assign(
        std::make_move_iterator(terminal_errors.begin()),
        std::make_move_iterator(terminal_errors.end()));
    return result;
}

std::string cue_plan_semantic_signature(
    const AcbCueGraph& graph,
    const AcbCuePlaybackPlan& plan) {
    return semantic_plan_signature(graph, plan);
}

static std::expected<AcbCuePlaybackPlan, std::string> resolve_plan_awb_entries(
    const AcbContainer& acb,
    AcbCuePlaybackPlan plan) {
    // TODO(acb-multi-awb): Resolve each streamed clip through its
    // StreamAwbPortNo and named StreamAwb slot. Streaming==2 requires both the
    // embedded prefetch bank and the external full-stream bank at runtime.
    if (!acb.has_embedded_awb() && !acb.companion_awb_path()) {
        return plan;
    }

    std::map<uint32_t, WaveformAwbEntry> resolved;
    std::set<uint32_t> attempted;
    for (auto& block : plan.blocks) {
        for (auto& clip : block.clips) {
            if (attempted.insert(clip.waveform_index).second) {
                auto entry = acb.waveform_awb_entry(clip.waveform_index);
                if (!entry) {
                    plan.diagnostics.push_back(
                        "waveform " + std::to_string(clip.waveform_index) +
                        " AWB provenance is unresolved: " + entry.error());
                } else {
                    resolved.emplace(clip.waveform_index, *entry);
                }
            }
            const auto entry = resolved.find(clip.waveform_index);
            if (entry == resolved.end()) {
                continue;
            }
            clip.awb_wave_id = entry->second.wave_id;
            clip.awb_stream_index = entry->second.awb_index;
            clip.awb_bank = entry->second.stream_bank
                ? AcbCueAwbBank::stream
                : AcbCueAwbBank::memory;
        }
    }
    return plan;
}

std::expected<AcbCuePlaybackPlan, std::string> plan_cue_playback(
    const AcbContainer& acb,
    uint32_t cue_index,
    const AcbCueRenderOptions& options) {
    auto plan = plan_cue_playback(acb.cue_graph(), cue_index, options);
    if (!plan) {
        return std::unexpected(plan.error());
    }
    return resolve_plan_awb_entries(acb, std::move(*plan));
}

std::expected<AcbCuePlaybackPlan, std::string> plan_cue_playback(
    const AcbContainer& acb,
    uint32_t cue_index,
    std::span<const AcbCueChoiceSelection> choices,
    const AcbCueRenderOptions& options) {
    auto plan = plan_cue_playback(
        acb.cue_graph(), cue_index, choices, options);
    if (!plan) {
        return std::unexpected(plan.error());
    }
    return resolve_plan_awb_entries(acb, std::move(*plan));
}

std::expected<AcbRenderedCue, std::string> render_cue(
    const AcbContainer& acb,
    uint32_t cue_index,
    const AcbCueRenderOptions& options) {
    auto plan = plan_cue_playback(acb, cue_index, options);
    if (!plan) return std::unexpected(plan.error());
    return render_cue_plan(acb, std::move(*plan), options);
}

std::expected<AcbRenderedCue, std::string> render_cue_plan(
    const AcbContainer& acb,
    AcbCuePlaybackPlan plan,
    const AcbCueRenderOptions& options) {
    // TODO(acb-native-loops): Carry codec/waveform loop points through full
    // cue rendering and CriStudio preview instead of treating decoded PCM as
    // one finite clip. Keep block-loop scheduling distinct from codec loops.
    uint16_t hca_subkey = options.hca_subkey.value_or(0);
    if (!options.hca_subkey) {
        auto subkey = acb.awb_subkey();
        if (!subkey) return std::unexpected(subkey.error());
        hca_subkey = *subkey;
    }

    // Logical waveform rows may reuse one physical AWB entry. Keep decoded PCM
    // only for this plan so reuse is cheap without retaining a bank-sized cache.
    std::map<DecodedSourceKey, DecodedWaveform> decoded;
    const auto decode_waveform = [&](uint32_t waveform_index)
        -> std::expected<std::reference_wrapper<const DecodedWaveform>, std::string> {
        auto codec = acb.waveform_codec(waveform_index);
        if (!codec) return std::unexpected(codec.error());
        if (*codec != awb::EntryCodec::Hca && *codec != awb::EntryCodec::Adx) {
            return std::unexpected(
                "ACB cue render failed: waveform " +
                std::to_string(waveform_index) + " uses unsupported codec `" +
                std::string(awb::entry_codec_name(*codec)) + "`");
        }

        DecodedSourceKey source{
            .index = waveform_index,
            .codec = *codec,
        };
        if (const auto entry = acb.waveform_awb_entry(waveform_index)) {
            source.index = entry->awb_index;
            source.bank = entry->stream_bank
                ? AcbCueAwbBank::stream
                : AcbCueAwbBank::memory;
            source.physical_awb_entry = true;
        }
        if (const auto it = decoded.find(source); it != decoded.end()) {
            return std::cref(it->second);
        }

        auto data = acb.extract_waveform_data(waveform_index);
        if (!data) {
            return std::unexpected(
                "ACB cue render failed for waveform " +
                std::to_string(waveform_index) + ": " + data.error());
        }

        DecodedWaveform waveform;
        switch (*codec) {
            case awb::EntryCodec::Hca: {
                auto audio = hca::Hca::load(*data);
                if (!audio) return std::unexpected(audio.error());
                auto pcm = audio->decode(options.hca_keycode, hca_subkey);
                if (!pcm) return std::unexpected(pcm.error());
                waveform.sample_rate = audio->header().fmt.sample_rate;
                waveform.channels = audio->header().fmt.channel_count;
                waveform.pcm = std::move(*pcm);
                break;
            }
            case awb::EntryCodec::Adx: {
                auto audio = adx::Adx::load(*data);
                if (!audio) return std::unexpected(audio.error());
                auto pcm = audio->decode();
                if (!pcm) return std::unexpected(pcm.error());
                waveform.sample_rate = pcm->sample_rate;
                waveform.channels = pcm->channels;
                waveform.pcm = std::move(pcm->pcm_data);
                break;
            }
            default:
                std::unreachable();
        }
        const auto [it, inserted] = decoded.emplace(
            source, std::move(waveform));
        (void)inserted;
        return std::cref(it->second);
    };

    uint32_t output_rate = 0;
    uint8_t output_channels = 0;
    std::vector<int16_t> output;
    std::vector<AcbRenderedBlockRange> block_ranges;
    block_ranges.reserve(plan.blocks.size());
    for (const auto& block : plan.blocks) {
        for (const auto& clip : block.clips) {
            auto waveform = decode_waveform(clip.waveform_index);
            if (!waveform) return std::unexpected(waveform.error());
            const auto& audio = waveform->get();
            if (output_rate == 0) {
                output_rate = audio.sample_rate;
                output_channels = audio.channels;
            } else if (
                audio.sample_rate != output_rate ||
                audio.channels != output_channels) {
                return std::unexpected(
                    "ACB cue render failed: mixed sample rates or channel counts "
                    "require resampling, which is not implemented");
            }
        }
    }
    if (output_rate == 0 || output_channels == 0) {
        return std::unexpected("ACB cue render failed: cue produced no decodable audio");
    }

    // The worst signed PCM16 contribution is -32768, so this many simultaneous
    // layers still fit exactly in an int32 accumulator.
    constexpr size_t max_int32_mix_layers =
        static_cast<size_t>(std::numeric_limits<int32_t>::max()) /
        static_cast<size_t>(-std::numeric_limits<int16_t>::min());
    std::vector<size_t> block_sample_counts(plan.blocks.size(), 0);
    size_t output_sample_count = 0;
    size_t max_int32_mix_samples = 0;
    size_t max_int64_mix_samples = 0;
    for (size_t block_index = 0; block_index < plan.blocks.size(); ++block_index) {
        const auto& block = plan.blocks[block_index];
        if (block.skipped_empty_hold) {
            continue;
        }
        uint64_t block_frames = 0;
        for (const auto& clip : block.clips) {
            auto waveform = decode_waveform(clip.waveform_index);
            if (!waveform) return std::unexpected(waveform.error());
            const auto& audio = waveform->get();
            const uint64_t start_frame =
                frames_for_us(static_cast<uint64_t>(clip.start_time_us), output_rate);
            block_frames = std::max(
                block_frames,
                start_frame + audio.pcm.size() / output_channels);
        }
        if (block.duration_us != 0) {
            block_frames = frames_for_us(block.duration_us, output_rate);
        }
        if (block_frames >
            std::numeric_limits<size_t>::max() / output_channels) {
            return std::unexpected("ACB cue render failed: block PCM size overflows");
        }
        const size_t block_samples =
            static_cast<size_t>(block_frames) * output_channels;
        const uint64_t total_plays =
            static_cast<uint64_t>(block.render_loop_count) + 1;
        if (block_samples >
            (std::numeric_limits<size_t>::max() - output_sample_count) /
                total_plays) {
            return std::unexpected("ACB cue render failed: output PCM size overflows");
        }
        block_sample_counts[block_index] = block_samples;
        output_sample_count += block_samples * total_plays;
        if (block.clips.size() > 1) {
            auto& max_mix_samples =
                block.clips.size() <= max_int32_mix_layers
                ? max_int32_mix_samples
                : max_int64_mix_samples;
            max_mix_samples = std::max(max_mix_samples, block_samples);
        }
    }
    output.reserve(output_sample_count);
    // Reuse one uninitialized scratch allocation for every layered block. Each
    // block initializes all samples from its first clip before accumulating.
    auto int32_mix_storage = max_int32_mix_samples == 0
        ? nullptr
        : std::make_unique_for_overwrite<int32_t[]>(max_int32_mix_samples);
    auto int64_mix_storage = max_int64_mix_samples == 0
        ? nullptr
        : std::make_unique_for_overwrite<int64_t[]>(max_int64_mix_samples);
    const auto block_start_sample = [&](int64_t start_time_us, size_t block_samples) {
        const uint64_t start_frame =
            frames_for_us(static_cast<uint64_t>(start_time_us), output_rate);
        const size_t block_frames = block_samples / output_channels;
        return start_frame >= block_frames
            ? block_samples
            : static_cast<size_t>(start_frame) * output_channels;
    };

    for (size_t block_index = 0; block_index < plan.blocks.size(); ++block_index) {
        const auto& block = plan.blocks[block_index];
        if (block.skipped_empty_hold) {
            continue;
        }
        const size_t block_samples = block_sample_counts[block_index];
        const size_t block_output_start = output.size();
        block_ranges.push_back({
            .plan_block_index = static_cast<uint32_t>(block_index),
            .start_sample = block_output_start / output_channels,
            .end_sample =
                (block_output_start + block_samples) / output_channels,
        });
        if (block_samples == 0 || block.clips.empty()) {
            output.insert(output.end(), block_samples, int16_t{0});
        } else if (block.clips.size() == 1) {
            const auto& clip = block.clips.front();
            auto waveform = decode_waveform(clip.waveform_index);
            if (!waveform) return std::unexpected(waveform.error());
            const auto& audio = waveform->get();
            const size_t start_sample =
                block_start_sample(clip.start_time_us, block_samples);
            const size_t prefix_samples = start_sample;
            const size_t audio_samples = std::min(
                audio.pcm.size(), block_samples - prefix_samples);
            output.insert(output.end(), prefix_samples, int16_t{0});
            output.insert(
                output.end(),
                audio.pcm.begin(),
                audio.pcm.begin() + static_cast<std::ptrdiff_t>(audio_samples));
            output.insert(
                output.end(),
                block_samples - prefix_samples - audio_samples,
                int16_t{0});
        } else {
            const auto mix_clips = [&]<typename MixSample>(MixSample* mixed)
                -> std::expected<void, std::string> {
                const auto initialize_clip = [&](const AcbCueClipPlan& clip)
                    -> std::expected<void, std::string> {
                    auto waveform = decode_waveform(clip.waveform_index);
                    if (!waveform) return std::unexpected(waveform.error());
                    const auto& audio = waveform->get();
                    const size_t start_sample =
                        block_start_sample(clip.start_time_us, block_samples);
                    const size_t prefix_samples = start_sample;
                    const size_t count = std::min(
                        audio.pcm.size(),
                        block_samples - prefix_samples);
                    std::fill_n(mixed, prefix_samples, MixSample{0});
                    // A widening store avoids a zero-plus-first-clip
                    // read-modify-write dependency in the hot mixing loop.
                    std::transform(
                        audio.pcm.begin(),
                        audio.pcm.begin() + static_cast<std::ptrdiff_t>(count),
                        mixed + prefix_samples,
                        [](int16_t sample) {
                            return static_cast<MixSample>(sample);
                        });
                    std::fill(
                        mixed + prefix_samples + count,
                        mixed + block_samples,
                        MixSample{0});
                    return {};
                };
                if (auto initialized = initialize_clip(block.clips.front());
                    !initialized) {
                    return initialized;
                }

                for (size_t clip_index = 1;
                     clip_index < block.clips.size();
                     ++clip_index) {
                    const auto& clip = block.clips[clip_index];
                    auto waveform = decode_waveform(clip.waveform_index);
                    if (!waveform) return std::unexpected(waveform.error());
                    const auto& audio = waveform->get();
                    const size_t start_sample =
                        block_start_sample(clip.start_time_us, block_samples);
                    const size_t count = std::min(
                        audio.pcm.size(), block_samples - start_sample);
                    for (size_t sample = 0; sample < count; ++sample) {
                        mixed[start_sample + sample] += audio.pcm[sample];
                    }
                }
                output.resize(block_output_start + block_samples);
                for (size_t sample = 0; sample < block_samples; ++sample) {
                    output[block_output_start + sample] =
                        saturate_sample(mixed[sample]);
                }
                return {};
            };

            auto mixed = block.clips.size() <= max_int32_mix_layers
                ? mix_clips.template operator()<int32_t>(int32_mix_storage.get())
                : mix_clips.template operator()<int64_t>(int64_mix_storage.get());
            if (!mixed) {
                return std::unexpected(mixed.error());
            }
        }
        const uint64_t total_plays =
            static_cast<uint64_t>(block.render_loop_count) + 1;
        for (uint64_t play = 1; play < total_plays; ++play) {
            for (size_t sample = 0; sample < block_samples; ++sample) {
                output.push_back(output[block_output_start + sample]);
            }
        }
    }

    // TODO(acb-runtime): Model runtime transitions, live actions, dynamic
    // selector changes, gains, and transition curves after their ordering and
    // scheduling semantics are verified against the official runtime.
    plan.diagnostics.push_back(
        "waveform gain, envelopes, transition curves, and runtime selector/action "
        "changes are not applied by the static renderer");
    return AcbRenderedCue{
        .plan = std::move(plan),
        .sample_rate = output_rate,
        .channels = output_channels,
        .pcm = std::move(output),
        .block_ranges = std::move(block_ranges),
    };
}

std::expected<void, std::string> extract_cue(
    const AcbContainer& acb,
    uint32_t cue_index,
    const std::filesystem::path& output_path,
    const AcbCueRenderOptions& options) {
    auto rendered = render_cue(acb, cue_index, options);
    if (!rendered) return std::unexpected(rendered.error());
    return wav::WavContainer::write(
        output_path.string(),
        rendered->pcm,
        rendered->sample_rate,
        rendered->channels);
}

std::expected<void, std::string> extract_cue_plan(
    const AcbContainer& acb,
    AcbCuePlaybackPlan plan,
    const std::filesystem::path& output_path,
    const AcbCueRenderOptions& options) {
    auto rendered = render_cue_plan(acb, std::move(plan), options);
    if (!rendered) return std::unexpected(rendered.error());
    return wav::WavContainer::write(
        output_path.string(),
        rendered->pcm,
        rendered->sample_rate,
        rendered->channels);
}

std::string cue_filename(
    const AcbContainer& acb,
    uint32_t cue_index,
    bool include_index_prefix) {
    std::string result;
    if (include_index_prefix) {
        result = std::to_string(cue_index + 1);
        result += '_';
    }
    result += safe_cue_name(acb.cue_graph().cue_name(cue_index));
    result += ".wav";
    return result;
}

} // namespace cricodecs::acb
