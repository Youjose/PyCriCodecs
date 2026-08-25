#pragma once
/**
 * @file acb_cue_renderer.hpp
 * @brief Deterministic, static playback planning and PCM rendering for ACB cues.
 *
 * The CRI runtime can keep an infinite block active until a game action changes
 * it. A file exporter cannot observe that action, so the policy below makes
 * that otherwise implicit decision explicit and inspectable.
 */

#include "acb_container.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cricodecs::acb {

struct AcbCueBlockLoopOverride {
    /// Zero-based position in BlockSequence.BlockIndex order.
    uint32_t block_position = 0;
    /// Number of repeats after the block's initial play.
    uint32_t loop_count = 0;
};

struct AcbCueRenderOptions {
    /// Repeats after the initial play for an audio-bearing infinite block.
    uint32_t infinite_block_loop_count = 0;
    /// Continue to the next block after the finite export substitute.
    bool advance_after_infinite_block = true;
    /// Render infinite holding blocks that schedule no waveform material.
    bool include_empty_infinite_blocks = false;
    /// Per-position repeat overrides, applied after the file's loop count.
    std::vector<AcbCueBlockLoopOverride> block_loop_overrides;
    uint64_t hca_keycode = 0;
    std::optional<uint16_t> hca_subkey;
};

using AcbCueAwbBank = AcbAwbBank;

struct AcbCueClipPlan {
    uint32_t waveform_index = 0;
    int64_t start_time_us = 0;
    /// AWB/AFS2 wave ID stored in the ACB waveform row.
    std::optional<uint16_t> awb_wave_id;
    /// Zero-based physical stream/file index in the resolved AWB.
    std::optional<uint32_t> awb_stream_index;
    /// Memory or stream bank selected by the ACB, refined after resolving an AWB.
    std::optional<AcbCueAwbBank> awb_bank;
    /// StreamAwb slot/port used by streamed clips; absent for the memory bank.
    std::optional<uint16_t> awb_port_no;
};

struct AcbCueBlockPlan {
    std::optional<uint32_t> block_position;
    std::optional<uint32_t> block_index;
    std::string name;
    uint64_t duration_us = 0;
    int32_t authored_loop_count = 0;
    uint32_t render_loop_count = 0;
    bool forced_advance = false;
    bool skipped_empty_hold = false;
    std::vector<AcbCueClipPlan> clips;
};

struct AcbCuePlaybackPlan {
    uint32_t cue_index = 0;
    uint32_t cue_id = 0;
    std::string cue_name;
    std::vector<AcbCueBlockPlan> blocks;
};

/** Exact frame range occupied by one rendered block iteration. */
struct AcbRenderedBlockRange {
    /// Index into AcbRenderedCue::plan.blocks.
    uint32_t plan_block_index = 0;
    /// Interleaved-PCM-independent frame offsets in the rendered cue.
    uint64_t start_sample = 0;
    uint64_t end_sample = 0;
};

enum class AcbRenderedLoopKind : uint8_t {
    authored_block,
    native_waveform,
};

/** Loop range that remains valid after cue scheduling and mixing. */
struct AcbRenderedLoop {
    AcbRenderedLoopKind kind = AcbRenderedLoopKind::native_waveform;
    uint32_t plan_block_index = 0;
    /// Present for a codec loop sourced from one waveform row.
    std::optional<uint32_t> waveform_index;
    /// Frame offsets in the fully rendered cue; end_sample is exclusive.
    uint64_t start_sample = 0;
    uint64_t end_sample = 0;
};

enum class AcbCueChoiceDomain : uint8_t {
    sequence_track,
    synth_reference,
};

/**
 * One explicit runtime choice used to materialize a static cue path.
 *
 * occurrence distinguishes repeated visits to the same row. Selector
 * names/values are populated when compact command metadata identifies them;
 * random/sequential modes normally leave those strings empty.
 */
struct AcbCueChoiceSelection {
    AcbCueChoiceDomain domain = AcbCueChoiceDomain::sequence_track;
    uint32_t node_index = 0;
    uint32_t occurrence = 0;
    uint32_t option_index = 0;
    uint8_t mode = 0;
    std::string selector_name;
    std::string selector_value;
};

struct AcbCuePlanVariant {
    AcbCuePlaybackPlan plan;
    /// Every choice path that produces this audio plan.
    std::vector<std::vector<AcbCueChoiceSelection>> paths;
};

struct AcbCueTerminalPath {
    std::vector<AcbCueChoiceSelection> choices;
    std::string error;
};

struct AcbCuePlanEnumeration {
    uint32_t cue_index = 0;
    /// Semantically unique playable plans for this cue.
    std::vector<AcbCuePlanVariant> variants;
    /// Non-choice terminal failures, retained for inspection/control cues.
    std::vector<std::string> terminal_errors;
    /// Choice-preserving terminal paths used by the higher-level action resolver.
    std::vector<AcbCueTerminalPath> terminal_paths;
    uint64_t explored_paths = 0;
};

struct AcbCueEnumerationOptions {
    AcbCueRenderOptions render;
    /// Hard guard against malformed or combinatorially explosive cue graphs.
    uint64_t max_paths = 65'536;
};

struct AcbRenderedCue {
    AcbCuePlaybackPlan plan;
    uint32_t sample_rate = 0;
    uint8_t channels = 0;
    std::vector<int16_t> pcm;
    /// One entry per non-skipped block, covering its first rendered iteration.
    std::vector<AcbRenderedBlockRange> block_ranges;
    /// Authored block and compatible native codec loops in rendered coordinates.
    std::vector<AcbRenderedLoop> loops;
};

/** Builds a loop-aware PCM16 WAV from a rendered cue. */
[[nodiscard]] std::expected<std::vector<uint8_t>, std::string>
build_rendered_cue_wav(const AcbRenderedCue& rendered);

[[nodiscard]] std::expected<AcbCuePlaybackPlan, std::string> plan_cue_playback(
    const AcbCueGraph& graph,
    uint32_t cue_index,
    const AcbCueRenderOptions& options = {});

[[nodiscard]] std::expected<AcbCuePlaybackPlan, std::string> plan_cue_playback(
    const AcbContainer& acb,
    uint32_t cue_index,
    const AcbCueRenderOptions& options = {});

[[nodiscard]] std::expected<AcbCuePlaybackPlan, std::string> plan_cue_playback(
    const AcbCueGraph& graph,
    uint32_t cue_index,
    std::span<const AcbCueChoiceSelection> choices,
    const AcbCueRenderOptions& options = {});

[[nodiscard]] std::expected<AcbCuePlaybackPlan, std::string> plan_cue_playback(
    const AcbContainer& acb,
    uint32_t cue_index,
    std::span<const AcbCueChoiceSelection> choices,
    const AcbCueRenderOptions& options = {});

/**
 * Enumerates possible static activations of one cue.
 *
 * Stateful modes such as Sequential, Shuffle, and RandomNoRepeat produce their
 * possible next-track choices; this is not an emulation of history retained
 * across repeated runtime cue invocations.
 */
[[nodiscard]] std::expected<AcbCuePlanEnumeration, std::string>
enumerate_cue_playback(
    const AcbCueGraph& graph,
    uint32_t cue_index,
    const AcbCueEnumerationOptions& options = {});

[[nodiscard]] std::string cue_plan_semantic_signature(
    const AcbCueGraph& graph,
    const AcbCuePlaybackPlan& plan);

[[nodiscard]] std::expected<AcbRenderedCue, std::string> render_cue(
    const AcbContainer& acb,
    uint32_t cue_index,
    const AcbCueRenderOptions& options = {});

/**
 * Renders an already-resolved static cue plan.
 *
 * The plan must have been produced from this ACB's cue graph. This overload is
 * used after selector/action path enumeration so the chosen path is not
 * discarded by replanning from only the cue index.
 */
[[nodiscard]] std::expected<AcbRenderedCue, std::string> render_cue_plan(
    const AcbContainer& acb,
    AcbCuePlaybackPlan plan,
    const AcbCueRenderOptions& options = {});

[[nodiscard]] std::expected<void, std::string> extract_cue(
    const AcbContainer& acb,
    uint32_t cue_index,
    const std::filesystem::path& output_path,
    const AcbCueRenderOptions& options = {});

[[nodiscard]] std::expected<void, std::string> extract_cue_plan(
    const AcbContainer& acb,
    AcbCuePlaybackPlan plan,
    const std::filesystem::path& output_path,
    const AcbCueRenderOptions& options = {});

[[nodiscard]] std::string cue_filename(
    const AcbContainer& acb,
    uint32_t cue_index,
    bool include_index_prefix = false);

[[nodiscard]] std::string sanitize_cue_filename_component(std::string_view text);

} // namespace cricodecs::acb
