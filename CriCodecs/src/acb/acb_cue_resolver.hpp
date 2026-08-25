#pragma once
/**
 * @file acb_cue_resolver.hpp
 * @brief Resolve selectors and actions into playable ACB cue paths.
 */

#include "acb_cue_renderer.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace cricodecs::acb {

struct AcbCueSelectorValue {
    std::string name;
    std::string value;
};

struct AcbCuePlanSource {
    uint32_t source_cue_index = 0;
    uint32_t source_cue_id = 0;
    std::string source_cue_name;
    uint32_t terminal_cue_index = 0;
    std::vector<uint32_t> action_cue_chain;
    std::vector<AcbCueSelectorValue> selector_values;
    std::vector<std::vector<AcbCueChoiceSelection>> paths;
};

struct AcbResolvedCuePlan {
    AcbCuePlaybackPlan plan;
    std::vector<AcbCuePlanSource> sources;
};

struct AcbCueSheetResolution {
    std::vector<AcbResolvedCuePlan> plans;
    std::vector<uint32_t> non_playable_cues;
};

struct AcbCueSheetResolveOptions {
    AcbCueEnumerationOptions enumeration;
    std::vector<AcbCueSelectorValue> selector_values;
    uint32_t max_action_depth = 64;
};

/**
 * Enumerates playable cue paths, executes statically resolvable selector-set
 * and Start actions, and deduplicates equivalent audio plans across cue names.
 *
 * Live-only operations such as block-transition, mute, stop, pause, and
 * playback-parameter actions remain non-audio control endpoints.
 */
[[nodiscard]] std::expected<AcbCueSheetResolution, std::string>
resolve_cue_sheet_playback(
    const AcbCueGraph& graph,
    const AcbCueSheetResolveOptions& options = {});

[[nodiscard]] std::expected<AcbCueSheetResolution, std::string>
resolve_cue_sheet_playback(
    const AcbContainer& acb,
    const AcbCueSheetResolveOptions& options = {});

[[nodiscard]] std::expected<AcbCueSheetResolution, std::string>
resolve_cue_playback_paths(
    const AcbCueGraph& graph,
    uint32_t cue_index,
    const AcbCueSheetResolveOptions& options = {});

[[nodiscard]] std::expected<AcbCueSheetResolution, std::string>
resolve_cue_playback_paths(
    const AcbContainer& acb,
    uint32_t cue_index,
    const AcbCueSheetResolveOptions& options = {});

/**
 * Builds stable, filesystem-safe WAV names for a resolved plan set.
 *
 * Selector labels are added when one cue name resolves to multiple
 * distinct plans. A variant ordinal remains as the collision fallback.
 */
[[nodiscard]] std::vector<std::string> cue_plan_filenames(
    const AcbCueSheetResolution& resolution,
    bool include_index_prefix = true);

} // namespace cricodecs::acb
