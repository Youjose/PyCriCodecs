#pragma once
/**
 * @file acb_cue_graph.hpp
 * @brief Lossless, inspectable ACB cue and command graph.
 *
 * This model is intentionally separate from AcbContainer's waveform extraction
 * surface. It preserves table-row identities, reference lists, raw
 * command streams, and conservative command interpretations without attempting
 * to emulate the CRI Atom runtime.
 */

#include "acb_commands.hpp"

#include "../utilities/text_encoding.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cricodecs::acb {

inline constexpr uint16_t invalid_acb_index = 0xFFFF;

enum class AcbCueCommandMeaning : uint8_t {
    unknown,
    terminator,
    target_reference,
    wait_milliseconds,
    wait_submillisecond,
    mute,
    category_information,
    cue_limit_information,
    runtime_counter_add,
    runtime_flag_write,
    selector_name,
    selector_condition,
    bus_send_by_name,
    sequence_wait_item,
    sequence_wait_timer,
    stop_at_loop_end,
    sequence_start_milliseconds,
    sequence_start_random,
    sequence_start,
    midi_event,
    end_track_event,
    start_action,
    stop_action,
    pause_action,
    resume_action,
    stop_action_parameterized,
    playback_parameter,
    set_next_block,
    set_selector_label,
};

enum class AcbCommandTableKind : uint8_t {
    track_event,
    legacy_command,
    sequence_command,
    track_command,
    synth_command,
};

struct AcbCueCommand {
    uint16_t code = 0;
    AcbCommandDispatcher dispatcher = AcbCommandDispatcher::serialized_event;
    AcbCommandFamily family = AcbCommandFamily::unknown;
    AcbCueCommandMeaning meaning = AcbCueCommandMeaning::unknown;
    std::vector<uint8_t> payload;
    std::optional<AcbCommandTarget> target;
    std::optional<uint16_t> argument_u16;
    std::optional<uint16_t> argument_u16_2;
    std::optional<int16_t> argument_i16;
    std::optional<int64_t> time_advance_us;
};

struct AcbScheduledTarget {
    uint32_t command_index = 0;
    int64_t time_us = 0;
    AcbCommandTarget target;
};

struct AcbCueCommandStream {
    uint32_t row_index = 0;
    AcbCommandTableKind table_kind = AcbCommandTableKind::track_event;
    AcbCommandDispatcher dispatcher = AcbCommandDispatcher::serialized_event;
    std::vector<uint8_t> raw;
    std::vector<AcbCueCommand> commands;
    std::vector<AcbScheduledTarget> scheduled_targets;
    int64_t duration_us = 0;
};

struct AcbCueName {
    uint32_t row_index = 0;
    uint16_t cue_index = invalid_acb_index;
    std::string name;
    std::string name_raw;
};

struct AcbCueReference {
    uint16_t type = 0;
    uint16_t index = invalid_acb_index;
};

struct AcbCue {
    uint32_t row_index = 0;
    uint32_t cue_id = 0;
    AcbCueReference reference;
    std::vector<uint32_t> name_rows;
    uint32_t length = 0;
    uint16_t worksize = 0;
    uint16_t num_related_waveforms = 0;
    uint8_t header_visibility = 0;
};

struct AcbSynthReference {
    uint16_t type = 0;
    uint16_t index = invalid_acb_index;
};

struct AcbSynth {
    uint32_t row_index = 0;
    uint8_t type = 0;
    uint16_t command_index = invalid_acb_index;
    uint16_t action_track_start_index = invalid_acb_index;
    uint16_t num_action_tracks = 0;
    std::vector<uint8_t> reference_items_raw;
    std::vector<AcbSynthReference> reference_items;
    std::vector<uint8_t> track_values_raw;
    std::vector<uint16_t> track_values;
};

struct AcbTrack {
    uint32_t row_index = 0;
    uint16_t event_index = invalid_acb_index;
    uint16_t command_index = invalid_acb_index;
    uint8_t target_type = 0;
    std::string target_name;
    std::string target_name_raw;
    uint32_t target_id = 0;
    std::string target_acb_name;
    std::string target_acb_name_raw;
    uint8_t scope = 0;
    uint16_t target_track_no = invalid_acb_index;
};

struct AcbSequence {
    uint32_t row_index = 0;
    uint8_t type = 0;
    uint16_t playback_ratio = 0;
    uint16_t command_index = invalid_acb_index;
    std::vector<uint16_t> track_indices;
    std::vector<uint8_t> track_values_raw;
    std::vector<uint16_t> track_values;
    uint16_t action_track_start_index = invalid_acb_index;
    uint16_t num_action_tracks = 0;
    uint16_t watch_action_start_index = invalid_acb_index;
    uint16_t num_watch_actions = 0;
    uint16_t stop_action_start_index = invalid_acb_index;
    uint16_t num_stop_actions = 0;
};

struct AcbBlock {
    uint32_t row_index = 0;
    std::vector<uint16_t> track_indices;
    uint8_t playback_type = 0;
    uint16_t num_loops = 0;
    uint8_t transition_timing = 0;
    uint16_t transition_timing_value = 0;
    uint8_t jump_previous_behavior = 0;
    uint8_t jump_destination = 0;
    uint16_t name_index = invalid_acb_index;
    uint32_t length_ms = 0;
    uint16_t length_submillisecond = 0;
    uint32_t start_position_ms = 0;
    uint16_t start_position_submillisecond = 0;
    uint16_t action_track_start_index = invalid_acb_index;
    uint16_t num_action_tracks = 0;
    std::vector<uint16_t> destination_blocks;
    std::vector<uint8_t> destination_values_raw;

    [[nodiscard]] constexpr uint64_t duration_us() const noexcept {
        return static_cast<uint64_t>(length_ms) * 1000 + length_submillisecond;
    }

    [[nodiscard]] constexpr uint64_t start_position_us() const noexcept {
        return static_cast<uint64_t>(start_position_ms) * 1000 + start_position_submillisecond;
    }
};

struct AcbBlockSequence {
    uint32_t row_index = 0;
    uint8_t type = 0;
    uint16_t playback_ratio = 0;
    uint16_t command_index = invalid_acb_index;
    std::vector<uint16_t> track_indices;
    std::vector<uint16_t> block_indices;
    std::vector<uint8_t> track_values_raw;
    std::vector<uint16_t> track_values;
    uint16_t watch_action_start_index = invalid_acb_index;
    uint16_t num_watch_actions = 0;
    uint16_t stop_action_start_index = invalid_acb_index;
    uint16_t num_stop_actions = 0;
};

struct AcbCueWaveform {
    uint32_t row_index = 0;
    uint16_t id = invalid_acb_index;
    uint16_t memory_awb_id = invalid_acb_index;
    uint16_t stream_awb_id = invalid_acb_index;
    uint16_t stream_awb_port_no = invalid_acb_index;
    uint8_t streaming = 0;
    uint8_t encode_type = 0;
    uint8_t num_channels = 0;
    uint8_t loop_flag = 0;
    uint32_t sampling_rate = 0;
    uint32_t num_samples = 0;
    uint16_t extension_data = invalid_acb_index;
};

struct AcbWaveformExtension {
    uint32_t row_index = 0;
    uint32_t loop_start = 0;
    uint32_t loop_end = 0;
};

struct AcbStringValue {
    uint32_t row_index = 0;
    std::string value;
    std::string value_raw;
};

struct AcbOutsideLink {
    uint32_t row_index = 0;
    uint32_t cue_id = 0xFFFFFFFFu;
    uint16_t cue_name_string_index = invalid_acb_index;
    uint16_t acb_name_string_index = invalid_acb_index;
};

enum class AcbCueNodeKind : uint8_t {
    cue,
    waveform,
    synth,
    sequence,
    track,
    action_track,
    track_event,
    legacy_command,
    sequence_command,
    track_command,
    synth_command,
    block_sequence,
    block,
    outside_link,
};

enum class AcbCueEdgeKind : uint8_t {
    cue_reference,
    synth_reference,
    track,
    action_track,
    block,
    event_stream,
    parameter_stream,
    command_target,
    outside_link,
};

struct AcbCueNode {
    AcbCueNodeKind kind = AcbCueNodeKind::cue;
    uint32_t index = 0;

    [[nodiscard]] friend constexpr bool operator==(const AcbCueNode&, const AcbCueNode&) = default;
};

struct AcbCueEdge {
    AcbCueNode from;
    AcbCueNode to;
    AcbCueEdgeKind kind = AcbCueEdgeKind::cue_reference;
    uint32_t ordinal = 0;
};

struct AcbUnresolvedReference {
    AcbCueNode source;
    uint16_t type = 0;
    uint16_t index = invalid_acb_index;
    std::string reason;
};

struct AcbCueAssembly {
    uint32_t cue_index = 0;
    std::vector<AcbCueNode> nodes;
    std::vector<AcbCueEdge> edges;
    std::vector<AcbUnresolvedReference> unresolved;
    bool has_cycle = false;
};

/**
 * Two intentionally different cue-name projections for a waveform row.
 *
 * exact_cue_name_rows contains only cues whose playback graph reaches this
 * exact WaveformTable row. preferred_cue_name_rows is its nearest
 * cue (or all equally-near cues), which is normally the most specific useful
 * display/file label. associated_awb_cue_name_rows additionally identifies
 * all cues attached to the same physical memory/stream AWB asset.
 */
struct AcbWaveformCueView {
    uint32_t waveform_index = 0;
    std::vector<uint32_t> exact_cue_name_rows;
    std::vector<uint32_t> preferred_cue_name_rows;
    std::vector<uint32_t> associated_awb_cue_name_rows;
};

enum class AcbWaveformNameView : uint8_t {
    preferred,
    exact_waveform,
    associated_awb_asset,
};

class AcbCueGraph {
public:
    [[nodiscard]] static std::expected<AcbCueGraph, std::string> load(
        std::span<const uint8_t> data,
        const text::EncodingOptions& encoding = {});
    [[nodiscard]] static std::expected<AcbCueGraph, std::string> load(
        const std::filesystem::path& path,
        const text::EncodingOptions& encoding = {});

    [[nodiscard]] const std::vector<AcbCue>& cues() const noexcept { return m_cues; }
    [[nodiscard]] const std::vector<AcbCueName>& cue_names() const noexcept { return m_cue_names; }
    [[nodiscard]] const std::vector<AcbSynth>& synths() const noexcept { return m_synths; }
    [[nodiscard]] const std::vector<AcbSequence>& sequences() const noexcept { return m_sequences; }
    [[nodiscard]] const std::vector<AcbTrack>& tracks() const noexcept { return m_tracks; }
    [[nodiscard]] const std::vector<AcbTrack>& action_tracks() const noexcept { return m_action_tracks; }
    [[nodiscard]] const std::vector<AcbBlockSequence>& block_sequences() const noexcept { return m_block_sequences; }
    [[nodiscard]] const std::vector<AcbBlock>& blocks() const noexcept { return m_blocks; }
    [[nodiscard]] const std::vector<AcbCueWaveform>& waveforms() const noexcept { return m_waveforms; }
    [[nodiscard]] const std::vector<AcbWaveformExtension>& waveform_extensions() const noexcept {
        return m_waveform_extensions;
    }
    [[nodiscard]] const std::vector<AcbStringValue>& strings() const noexcept { return m_strings; }
    [[nodiscard]] const std::vector<AcbOutsideLink>& outside_links() const noexcept {
        return m_outside_links;
    }
    [[nodiscard]] bool has_embedded_awb() const noexcept { return m_has_embedded_awb; }

    [[nodiscard]] const std::vector<AcbCueCommandStream>& track_events() const noexcept {
        return commands(AcbCommandTableKind::track_event);
    }
    [[nodiscard]] const std::vector<AcbCueCommandStream>& legacy_commands() const noexcept {
        return commands(AcbCommandTableKind::legacy_command);
    }
    [[nodiscard]] const std::vector<AcbCueCommandStream>& sequence_commands() const noexcept {
        return commands(AcbCommandTableKind::sequence_command);
    }
    [[nodiscard]] const std::vector<AcbCueCommandStream>& track_commands() const noexcept {
        return commands(AcbCommandTableKind::track_command);
    }
    [[nodiscard]] const std::vector<AcbCueCommandStream>& synth_commands() const noexcept {
        return commands(AcbCommandTableKind::synth_command);
    }

    [[nodiscard]] const AcbCue* cue_by_id(uint32_t cue_id) const noexcept;
    [[nodiscard]] std::string_view cue_name(uint32_t cue_index) const noexcept;
    [[nodiscard]] std::string_view string_value(uint32_t string_index) const noexcept;
    [[nodiscard]] std::string_view outside_link_cue_name(uint32_t link_index) const noexcept;
    [[nodiscard]] std::string_view outside_link_acb_name(uint32_t link_index) const noexcept;
    [[nodiscard]] const AcbCueCommandStream* command_stream(
        AcbCommandTableKind kind,
        uint32_t row_index) const noexcept;
    [[nodiscard]] std::expected<AcbCueAssembly, std::string> assemble_cue(uint32_t cue_index) const;
    [[nodiscard]] std::expected<std::vector<AcbWaveformCueView>, std::string>
        waveform_cue_views() const;
    /// Prefer this bulk form when naming a flat waveform list.
    [[nodiscard]] std::expected<std::vector<std::string>, std::string> waveform_names(
        AcbWaveformNameView view = AcbWaveformNameView::preferred,
        bool raw = false) const;
    /// Convenience for one row; bulk callers should use waveform_names().
    [[nodiscard]] std::expected<std::string, std::string> waveform_name(
        uint32_t waveform_index,
        AcbWaveformNameView view = AcbWaveformNameView::preferred,
        bool raw = false) const;

private:
    friend class AcbCueGraphParser;

    [[nodiscard]] const std::vector<AcbCueCommandStream>& commands(
        AcbCommandTableKind kind) const noexcept {
        return m_commands[static_cast<std::size_t>(kind)];
    }
    [[nodiscard]] std::vector<AcbCueCommandStream>& commands(
        AcbCommandTableKind kind) noexcept {
        return m_commands[static_cast<std::size_t>(kind)];
    }

    std::vector<AcbCue> m_cues;
    std::vector<AcbCueName> m_cue_names;
    std::vector<AcbSynth> m_synths;
    std::vector<AcbSequence> m_sequences;
    std::vector<AcbTrack> m_tracks;
    std::vector<AcbTrack> m_action_tracks;
    std::vector<AcbBlockSequence> m_block_sequences;
    std::vector<AcbBlock> m_blocks;
    std::vector<AcbCueWaveform> m_waveforms;
    std::vector<AcbWaveformExtension> m_waveform_extensions;
    std::vector<AcbStringValue> m_strings;
    std::vector<AcbOutsideLink> m_outside_links;

    std::array<std::vector<AcbCueCommandStream>, 5> m_commands;
    bool m_has_embedded_awb = false;
};

[[nodiscard]] constexpr std::string_view cue_command_meaning_name(
    AcbCueCommandMeaning meaning) noexcept {
    switch (meaning) {
        case AcbCueCommandMeaning::unknown:              return "unknown";
        case AcbCueCommandMeaning::terminator:           return "terminator";
        case AcbCueCommandMeaning::target_reference:     return "target_reference";
        case AcbCueCommandMeaning::wait_milliseconds:    return "wait_milliseconds";
        case AcbCueCommandMeaning::wait_submillisecond:  return "wait_submillisecond";
        case AcbCueCommandMeaning::mute:                 return "mute";
        case AcbCueCommandMeaning::category_information: return "category_information";
        case AcbCueCommandMeaning::cue_limit_information:return "cue_limit_information";
        case AcbCueCommandMeaning::runtime_counter_add:  return "runtime_counter_add";
        case AcbCueCommandMeaning::runtime_flag_write:   return "runtime_flag_write";
        case AcbCueCommandMeaning::selector_name:        return "selector_name";
        case AcbCueCommandMeaning::selector_condition:   return "selector_condition";
        case AcbCueCommandMeaning::bus_send_by_name:     return "bus_send_by_name";
        case AcbCueCommandMeaning::sequence_wait_item:   return "sequence_wait_item";
        case AcbCueCommandMeaning::sequence_wait_timer:  return "sequence_wait_timer";
        case AcbCueCommandMeaning::stop_at_loop_end:     return "stop_at_loop_end";
        case AcbCueCommandMeaning::sequence_start_milliseconds:
            return "sequence_start_milliseconds";
        case AcbCueCommandMeaning::sequence_start_random:return "sequence_start_random";
        case AcbCueCommandMeaning::sequence_start:       return "sequence_start";
        case AcbCueCommandMeaning::midi_event:           return "midi_event";
        case AcbCueCommandMeaning::end_track_event:      return "end_track_event";
        case AcbCueCommandMeaning::start_action:         return "start_action";
        case AcbCueCommandMeaning::stop_action:          return "stop_action";
        case AcbCueCommandMeaning::pause_action:         return "pause_action";
        case AcbCueCommandMeaning::resume_action:        return "resume_action";
        case AcbCueCommandMeaning::stop_action_parameterized:
            return "stop_action_parameterized";
        case AcbCueCommandMeaning::playback_parameter:   return "playback_parameter";
        case AcbCueCommandMeaning::set_next_block:       return "set_next_block";
        case AcbCueCommandMeaning::set_selector_label:   return "set_selector_label";
    }
    return "unknown";
}

/**
 * Names the Sequence.Type values implemented by the official SDK 3.46
 * playback switch. Unknown values remain inspectable as raw integers.
 */
[[nodiscard]] constexpr std::string_view sequence_type_name(uint8_t type) noexcept {
    switch (type) {
        case 0: return "polyphonic";
        case 1: return "sequential";
        case 2: return "shuffle";
        case 3: return "random";
        case 4: return "random_no_repeat";
        case 5: return "switch_game_variable";
        case 6: return "combo_sequential";
        case 7: return "switch_selector";
        case 8: return "track_transition_by_selector";
        default: return "unknown";
    }
}

/**
 * Names the ACB reference types confirmed by the official runtime dispatcher.
 */
[[nodiscard]] constexpr std::string_view reference_type_name(uint16_t type) noexcept {
    switch (type) {
        case 0: return "none";
        case 1: return "waveform";
        case 2: return "synth";
        case 3: return "sequence";
        case 5: return "outside_link";
        case 6: return "direct_synth";
        case 7: return "direct_sequence";
        case 8: return "block_sequence";
        case 9: return "direct_block_sequence";
        case 11: return "special_11";
        case 12: return "special_12";
        default: return "unknown";
    }
}

} // namespace cricodecs::acb
