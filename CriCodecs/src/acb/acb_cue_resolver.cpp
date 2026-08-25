/**
 * @file acb_cue_resolver.cpp
 * @brief Static selector/action resolution and cue-sheet plan deduplication.
 */

#include "acb_cue_resolver.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <tuple>
#include <utility>

namespace cricodecs::acb {

namespace {

using SelectorState = std::map<std::string, std::string, std::less<>>;
using CueSelectorMap = std::map<std::string, std::set<std::string>>;

struct ControlEffects {
    SelectorState selectors;
    std::vector<uint32_t> start_cue_indices;
};

const AcbCueCommandStream* action_stream(
    const AcbCueGraph& graph,
    const AcbTrack& action) {
    return graph.command_stream(
        graph.track_events().empty()
            ? AcbCommandTableKind::legacy_command
            : AcbCommandTableKind::track_event,
        action.command_index);
}

std::optional<uint32_t> cue_index_for_id(
    const AcbCueGraph& graph,
    uint32_t cue_id) {
    const auto* cue = graph.cue_by_id(cue_id);
    if (cue == nullptr) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(cue - graph.cues().data());
}

class ControlWalker {
public:
    ControlWalker(
        const AcbCueGraph& graph,
        std::span<const AcbCueChoiceSelection> choices)
        : m_graph(graph), m_choices(choices) {}

    std::expected<ControlEffects, std::string> walk(uint32_t cue_index) {
        if (cue_index >= m_graph.cues().size()) {
            return std::unexpected(
                "ACB cue action resolution failed: cue index is out of range");
        }
        const auto& cue = m_graph.cues()[cue_index];
        auto result = walk_reference(cue.reference.type, cue.reference.index);
        if (!result) {
            return std::unexpected(result.error());
        }
        return std::move(m_effects);
    }

private:
    std::expected<void, std::string> walk_reference(uint16_t type, uint16_t index) {
        const auto key = std::pair{type, index};
        if (m_active.size() >= 64 || !m_active.insert(key).second) {
            return std::unexpected(
                "ACB cue action resolution failed: cyclic or excessively deep reference");
        }
        const auto erase = [this, key] { m_active.erase(key); };

        std::expected<void, std::string> result;
        switch (type) {
            case 0:
            case 1:
                break;
            case 2:
            case 6:
                result = walk_synth(index);
                break;
            case 3:
            case 7:
                result = walk_sequence(index);
                break;
            case 5:
                result = std::unexpected(
                    "ACB cue action resolution failed: outside-link target is unresolved");
                break;
            case 8:
            case 9:
                result = walk_block_sequence(index);
                break;
            default:
                result = std::unexpected(
                    "ACB cue action resolution failed: unsupported reference type " +
                    std::to_string(type));
                break;
        }
        erase();
        return result;
    }

    std::expected<void, std::string> walk_synth(uint16_t index) {
        if (index >= m_graph.synths().size()) {
            return std::unexpected(
                "ACB cue action resolution failed: synth index is out of range");
        }
        const auto& synth = m_graph.synths()[index];
        process_actions(synth.action_track_start_index, synth.num_action_tracks);
        if (synth.reference_items.size() > 1 && synth.type != 0) {
            auto selected = selected_option(
                AcbCueChoiceDomain::synth_reference, index);
            if (!selected || *selected >= synth.reference_items.size()) {
                return std::unexpected(
                    "ACB cue action resolution failed: missing synth choice");
            }
            const auto& reference = synth.reference_items[*selected];
            return walk_reference(reference.type, reference.index);
        }
        for (const auto& reference : synth.reference_items) {
            auto result = walk_reference(reference.type, reference.index);
            if (!result) return result;
        }
        return {};
    }

    std::expected<void, std::string> walk_sequence(uint16_t index) {
        if (index >= m_graph.sequences().size()) {
            return std::unexpected(
                "ACB cue action resolution failed: sequence index is out of range");
        }
        const auto& sequence = m_graph.sequences()[index];
        process_actions(
            sequence.action_track_start_index, sequence.num_action_tracks);

        if (sequence.track_indices.size() > 1 && sequence.type != 0) {
            auto selected = selected_option(
                AcbCueChoiceDomain::sequence_track, index);
            if (!selected || *selected >= sequence.track_indices.size()) {
                return std::unexpected(
                    "ACB cue action resolution failed: missing sequence choice");
            }
            return walk_track(sequence.track_indices[*selected]);
        }
        for (const auto track_index : sequence.track_indices) {
            auto result = walk_track(track_index);
            if (!result) return result;
        }
        return {};
    }

    std::expected<void, std::string> walk_block_sequence(uint16_t index) {
        if (index >= m_graph.block_sequences().size()) {
            return std::unexpected(
                "ACB cue action resolution failed: block-sequence index is out of range");
        }
        const auto& sequence = m_graph.block_sequences()[index];
        for (const auto track_index : sequence.track_indices) {
            auto result = walk_track(track_index);
            if (!result) return result;
        }
        for (const auto block_index : sequence.block_indices) {
            if (block_index >= m_graph.blocks().size()) {
                return std::unexpected(
                    "ACB cue action resolution failed: block index is out of range");
            }
            const auto& block = m_graph.blocks()[block_index];
            for (const auto track_index : block.track_indices) {
                auto result = walk_track(track_index);
                if (!result) return result;
            }
        }
        return {};
    }

    std::expected<void, std::string> walk_track(uint16_t index) {
        if (index >= m_graph.tracks().size()) {
            return std::unexpected(
                "ACB cue action resolution failed: track index is out of range");
        }
        const auto& track = m_graph.tracks()[index];
        const auto* stream = m_graph.command_stream(
            m_graph.track_events().empty()
                ? AcbCommandTableKind::legacy_command
                : AcbCommandTableKind::track_event,
            track.event_index);
        if (stream == nullptr) {
            return {};
        }
        for (const auto& target : stream->scheduled_targets) {
            auto result = walk_reference(
                static_cast<uint16_t>(target.target.type),
                target.target.index);
            if (!result) return result;
        }
        return {};
    }

    void process_actions(uint16_t start, uint16_t count) {
        if (start == invalid_acb_index || count == 0) {
            return;
        }
        const size_t end = std::min(
            static_cast<size_t>(start) + count,
            m_graph.action_tracks().size());
        for (size_t index = start; index < end; ++index) {
            const auto& action = m_graph.action_tracks()[index];
            const auto* stream = action_stream(m_graph, action);
            if (stream == nullptr) {
                continue;
            }
            int64_t action_time_us = 0;
            for (const auto& command : stream->commands) {
                if (command.time_advance_us) {
                    action_time_us += *command.time_advance_us;
                    continue;
                }
                if (command.meaning == AcbCueCommandMeaning::set_selector_label) {
                    if (action_time_us != 0) {
                        continue;
                    }
                    std::string selector;
                    std::string value;
                    if (command.argument_u16_2 && command.argument_u16) {
                        selector = m_graph.string_value(*command.argument_u16);
                        value = m_graph.string_value(*command.argument_u16_2);
                    } else if (command.argument_u16) {
                        selector = action.target_name;
                        value = m_graph.string_value(*command.argument_u16);
                    }
                    if (!selector.empty() && !value.empty()) {
                        m_effects.selectors.insert_or_assign(
                            std::move(selector), std::move(value));
                    }
                } else if (command.meaning == AcbCueCommandMeaning::start_action) {
                    if (action_time_us != 0 || action.target_type != 1) continue;
                    const auto target = cue_index_for_id(m_graph, action.target_id);
                    if (!target) continue;
                    if (!std::ranges::contains(m_effects.start_cue_indices, *target)) {
                        m_effects.start_cue_indices.push_back(*target);
                    }
                }
            }
        }
    }

    std::optional<uint32_t> selected_option(
        AcbCueChoiceDomain domain,
        uint32_t node_index) {
        const auto key = std::pair{domain, node_index};
        const uint32_t occurrence = m_occurrences[key]++;
        const auto selected = std::ranges::find_if(
            m_choices,
            [=](const AcbCueChoiceSelection& choice) {
                return choice.domain == domain &&
                    choice.node_index == node_index &&
                    choice.occurrence == occurrence;
            });
        if (selected == m_choices.end()) {
            return std::nullopt;
        }
        return selected->option_index;
    }

    const AcbCueGraph& m_graph;
    std::span<const AcbCueChoiceSelection> m_choices;
    std::map<std::pair<AcbCueChoiceDomain, uint32_t>, uint32_t> m_occurrences;
    std::set<std::pair<uint16_t, uint16_t>> m_active;
    ControlEffects m_effects;
};

SelectorState make_selector_state(
    std::span<const AcbCueSelectorValue> values) {
    SelectorState result;
    for (const auto& value : values) {
        result.insert_or_assign(value.name, value.value);
    }
    return result;
}

std::vector<AcbCueSelectorValue> selector_values(const SelectorState& state) {
    std::vector<AcbCueSelectorValue> result;
    result.reserve(state.size());
    for (const auto& [name, value] : state) {
        result.push_back({.name = name, .value = value});
    }
    return result;
}

bool path_matches(
    std::span<const AcbCueChoiceSelection> path,
    const SelectorState& selectors) {
    for (const auto& choice : path) {
        if (choice.selector_name.empty()) {
            continue;
        }
        const auto selected = selectors.find(choice.selector_name);
        if (selected != selectors.end() &&
            selected->second != choice.selector_value) {
            return false;
        }
    }
    return true;
}

struct Candidate {
    AcbCuePlaybackPlan plan;
    std::vector<std::vector<AcbCueChoiceSelection>> paths;
    SelectorState selectors;
    std::vector<uint32_t> action_chain;
};

class CueResolver {
public:
    CueResolver(
        const AcbCueGraph& graph,
        const AcbCueSheetResolveOptions& options)
        : m_graph(graph), m_options(options) {}

    std::expected<AcbCueSheetResolution, std::string> resolve() {
        if (auto valid = validate_options(); !valid) return std::unexpected(valid.error());
        const auto selectors = make_selector_state(m_options.selector_values);
        for (uint32_t cue_index = 0;
             cue_index < m_graph.cues().size();
             ++cue_index) {
            if (auto resolved = resolve_into(cue_index, selectors); !resolved)
                return std::unexpected(resolved.error());
        }
        return std::move(m_result);
    }

    std::expected<AcbCueSheetResolution, std::string> resolve_one(
        uint32_t cue_index) {
        if (auto valid = validate_options(); !valid) return std::unexpected(valid.error());
        if (cue_index >= m_graph.cues().size()) {
            return std::unexpected(
                "ACB cue-sheet resolution failed: cue index is out of range");
        }
        const auto selectors = make_selector_state(m_options.selector_values);
        if (auto resolved = resolve_into(cue_index, selectors); !resolved)
            return std::unexpected(resolved.error());
        return std::move(m_result);
    }

private:
    std::expected<void, std::string> validate_options() const {
        if (m_options.max_action_depth == 0) {
            return std::unexpected(
                "ACB cue-sheet resolution failed: max_action_depth must be greater than zero");
        }
        return {};
    }

    std::expected<void, std::string> resolve_into(
        uint32_t cue_index,
        const SelectorState& selectors) {
        std::vector<uint32_t> active;
        auto candidates = resolve_cue(cue_index, selectors, active, 0);
        if (!candidates) return std::unexpected(candidates.error());
        if (candidates->empty()) {
            m_result.non_playable_cues.push_back(cue_index);
        } else {
            for (auto& candidate : *candidates)
                add_candidate(cue_index, std::move(candidate));
        }
        return {};
    }

    std::expected<std::vector<Candidate>, std::string> resolve_cue(
        uint32_t cue_index,
        const SelectorState& selectors,
        std::vector<uint32_t>& active,
        uint32_t depth) {
        if (depth >= m_options.max_action_depth) {
            return std::unexpected(
                "ACB cue-sheet resolution failed: action depth limit exceeded");
        }
        if (std::ranges::contains(active, cue_index)) {
            return std::unexpected(
                "ACB cue-sheet resolution failed: cyclic Start action chain");
        }
        active.push_back(cue_index);

        auto enumeration = enumerate_cue_playback(
            m_graph, cue_index, m_options.enumeration);
        if (!enumeration) {
            active.pop_back();
            return std::unexpected(enumeration.error());
        }

        std::vector<Candidate> result;
        for (const auto& variant : enumeration->variants) {
            Candidate candidate{
                .plan = variant.plan,
                .paths = {},
                .selectors = selectors,
                .action_chain = {},
            };
            for (const auto& path : variant.paths) {
                if (path_matches(path, selectors)) {
                    candidate.paths.push_back(path);
                }
            }
            if (!candidate.paths.empty()) {
                result.push_back(std::move(candidate));
            }
        }

        for (const auto& terminal : enumeration->terminal_paths) {
            if (!path_matches(terminal.choices, selectors)) {
                continue;
            }
            auto effects = ControlWalker(m_graph, terminal.choices).walk(cue_index);
            if (!effects) continue;
            if (effects->start_cue_indices.empty()) {
                continue;
            }
            if (effects->start_cue_indices.size() > 1) {
                continue;
            }

            auto next_selectors = selectors;
            for (const auto& [name, value] : effects->selectors) {
                next_selectors.insert_or_assign(name, value);
            }
            for (const auto target : effects->start_cue_indices) {
                auto nested = resolve_cue(
                    target, next_selectors, active, depth + 1);
                if (!nested) {
                    active.pop_back();
                    return std::unexpected(nested.error());
                }
                for (auto& candidate : *nested) {
                    candidate.action_chain.insert(
                        candidate.action_chain.begin(), target);
                    result.push_back(std::move(candidate));
                }
            }
        }

        active.pop_back();
        return result;
    }

    void add_candidate(uint32_t source_cue_index, Candidate candidate) {
        const auto signature =
            cue_plan_semantic_signature(m_graph, candidate.plan);
        const auto [it, inserted] =
            m_signature_to_plan.emplace(signature, m_result.plans.size());
        if (inserted) {
            m_result.plans.push_back({
                .plan = std::move(candidate.plan),
                .sources = {},
            });
        }
        auto& resolved = m_result.plans[it->second];
        const auto& source_cue = m_graph.cues()[source_cue_index];
        resolved.sources.push_back({
            .source_cue_index = source_cue_index,
            .source_cue_id = source_cue.cue_id,
            .source_cue_name = std::string(m_graph.cue_name(source_cue_index)),
            .terminal_cue_index = resolved.plan.cue_index,
            .action_cue_chain = std::move(candidate.action_chain),
            .selector_values = selector_values(candidate.selectors),
            .paths = std::move(candidate.paths),
        });
    }

    const AcbCueGraph& m_graph;
    const AcbCueSheetResolveOptions& m_options;
    AcbCueSheetResolution m_result;
    std::map<std::string, size_t> m_signature_to_plan;
};

[[nodiscard]] CueSelectorMap canonical_cue_selectors(
    const AcbResolvedCuePlan& resolved) {
    CueSelectorMap selectors;
    const bool has_terminal_source = std::ranges::any_of(
        resolved.sources,
        [&](const auto& source) {
            return source.source_cue_index == resolved.plan.cue_index;
        });
    for (const auto& source : resolved.sources) {
        if (has_terminal_source &&
            source.source_cue_index != resolved.plan.cue_index) {
            continue;
        }
        for (const auto& selector : source.selector_values) {
            if (!selector.name.empty() && !selector.value.empty()) {
                selectors[selector.name].insert(selector.value);
            }
        }
        for (const auto& path : source.paths) {
            for (const auto& choice : path) {
                if (!choice.selector_name.empty() &&
                    !choice.selector_value.empty()) {
                    selectors[choice.selector_name].insert(
                        choice.selector_value);
                }
            }
        }
    }
    return selectors;
}

[[nodiscard]] std::expected<AcbCueSheetResolution, std::string>
resolve_awb_provenance(
    const AcbContainer& acb,
    AcbCueSheetResolution resolved) {
    if (!acb.has_embedded_awb() && !acb.companion_awb_path()) {
        return resolved;
    }

    std::map<uint32_t, WaveformAwbEntry> entries;
    std::set<uint32_t> attempted;
    for (auto& resolved_plan : resolved.plans) {
        for (auto& block : resolved_plan.plan.blocks) {
            for (auto& clip : block.clips) {
                if (attempted.insert(clip.waveform_index).second) {
                    auto entry = acb.waveform_awb_entry(clip.waveform_index);
                    if (entry) {
                        entries.emplace(clip.waveform_index, *entry);
                    }
                }
                const auto entry = entries.find(clip.waveform_index);
                if (entry == entries.end()) {
                    continue;
                }
                clip.awb_wave_id = entry->second.wave_id;
                clip.awb_stream_index = entry->second.awb_index;
                clip.awb_bank = entry->second.stream_bank
                    ? AcbCueAwbBank::stream
                    : AcbCueAwbBank::memory;
            }
        }
    }
    return resolved;
}

} // namespace

std::expected<AcbCueSheetResolution, std::string> resolve_cue_sheet_playback(
    const AcbCueGraph& graph,
    const AcbCueSheetResolveOptions& options) {
    return CueResolver(graph, options).resolve();
}

std::expected<AcbCueSheetResolution, std::string> resolve_cue_sheet_playback(
    const AcbContainer& acb,
    const AcbCueSheetResolveOptions& options) {
    auto resolved = resolve_cue_sheet_playback(acb.cue_graph(), options);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }
    return resolve_awb_provenance(acb, std::move(*resolved));
}

std::expected<AcbCueSheetResolution, std::string> resolve_cue_playback_paths(
    const AcbCueGraph& graph,
    uint32_t cue_index,
    const AcbCueSheetResolveOptions& options) {
    return CueResolver(graph, options).resolve_one(cue_index);
}

std::expected<AcbCueSheetResolution, std::string> resolve_cue_playback_paths(
    const AcbContainer& acb,
    uint32_t cue_index,
    const AcbCueSheetResolveOptions& options) {
    auto resolved =
        resolve_cue_playback_paths(acb.cue_graph(), cue_index, options);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }
    return resolve_awb_provenance(acb, std::move(*resolved));
}

std::vector<std::string> cue_plan_filenames(
    const AcbCueSheetResolution& resolution,
    bool include_index_prefix) {
    std::vector<std::string> bases;
    std::vector<CueSelectorMap> selectors;
    bases.reserve(resolution.plans.size());
    selectors.reserve(resolution.plans.size());

    std::map<std::string, std::vector<size_t>> groups;
    for (size_t index = 0; index < resolution.plans.size(); ++index) {
        const auto& resolved = resolution.plans[index];
        auto base = sanitize_cue_filename_component(resolved.plan.cue_name);
        groups[base].push_back(index);
        bases.push_back(std::move(base));
        selectors.push_back(canonical_cue_selectors(resolved));
    }

    std::vector<std::string> suffixes(resolution.plans.size());
    for (const auto& group : groups | std::views::values) {
        if (group.size() < 2) {
            continue;
        }

        std::set<std::string> candidates;
        for (const auto plan_index : group) {
            for (const auto& [name, values] : selectors[plan_index]) {
                if (values.size() == 1) {
                    candidates.insert(name);
                }
            }
        }

        std::set<std::pair<size_t, size_t>> unresolved_pairs;
        for (size_t lhs = 0; lhs < group.size(); ++lhs) {
            for (size_t rhs = lhs + 1; rhs < group.size(); ++rhs) {
                unresolved_pairs.emplace(lhs, rhs);
            }
        }

        std::vector<std::string> selected;
        while (!unresolved_pairs.empty()) {
            size_t best_score = 0;
            auto best = candidates.end();
            for (auto candidate = candidates.begin();
                 candidate != candidates.end();
                 ++candidate) {
                size_t score = 0;
                for (const auto& [lhs, rhs] : unresolved_pairs) {
                    const auto lhs_values =
                        selectors[group[lhs]].find(*candidate);
                    const auto rhs_values =
                        selectors[group[rhs]].find(*candidate);
                    if (lhs_values != selectors[group[lhs]].end() &&
                        rhs_values != selectors[group[rhs]].end() &&
                        lhs_values->second.size() == 1 &&
                        rhs_values->second.size() == 1 &&
                        *lhs_values->second.begin() !=
                            *rhs_values->second.begin()) {
                        ++score;
                    }
                }
                if (score > best_score) {
                    best_score = score;
                    best = candidate;
                }
            }
            if (best == candidates.end() || best_score == 0) {
                break;
            }

            selected.push_back(*best);
            std::erase_if(
                unresolved_pairs,
                [&](const auto& pair) {
                    const auto& [lhs, rhs] = pair;
                    const auto lhs_values =
                        selectors[group[lhs]].find(*best);
                    const auto rhs_values =
                        selectors[group[rhs]].find(*best);
                    return
                        lhs_values != selectors[group[lhs]].end() &&
                        rhs_values != selectors[group[rhs]].end() &&
                        lhs_values->second.size() == 1 &&
                        rhs_values->second.size() == 1 &&
                        *lhs_values->second.begin() !=
                            *rhs_values->second.begin();
                });
            candidates.erase(best);
        }

        for (size_t ordinal = 0; ordinal < group.size(); ++ordinal) {
            const auto plan_index = group[ordinal];
            for (const auto& selector_name : selected) {
                const auto found = selectors[plan_index].find(selector_name);
                if (found == selectors[plan_index].end() ||
                    found->second.size() != 1) {
                    continue;
                }
                suffixes[plan_index] += "__";
                suffixes[plan_index] +=
                    sanitize_cue_filename_component(selector_name);
                suffixes[plan_index] += '-';
                suffixes[plan_index] +=
                    sanitize_cue_filename_component(*found->second.begin());
            }
            if (!unresolved_pairs.empty() || suffixes[plan_index].empty()) {
                suffixes[plan_index] += "__variant-";
                suffixes[plan_index] += std::to_string(ordinal + 1);
            }
        }

        std::map<std::string, size_t> sanitized_suffix_counts;
        for (const auto plan_index : group) {
            ++sanitized_suffix_counts[suffixes[plan_index]];
        }
        for (size_t ordinal = 0; ordinal < group.size(); ++ordinal) {
            const auto plan_index = group[ordinal];
            if (sanitized_suffix_counts[suffixes[plan_index]] > 1) {
                suffixes[plan_index] += "__variant-";
                suffixes[plan_index] += std::to_string(ordinal + 1);
            }
        }
    }

    const size_t width = std::max<size_t>(
        3, std::to_string(resolution.plans.size()).size());
    std::vector<std::string> filenames;
    filenames.reserve(resolution.plans.size());
    for (size_t index = 0; index < resolution.plans.size(); ++index) {
        std::ostringstream name;
        if (include_index_prefix) {
            name << std::setw(static_cast<int>(width)) << std::setfill('0')
                 << index + 1 << '_';
        }
        name << bases[index] << suffixes[index] << ".wav";
        filenames.push_back(std::move(name).str());
    }
    return filenames;
}

} // namespace cricodecs::acb
