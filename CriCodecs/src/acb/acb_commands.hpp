#pragma once
/**
 * @file acb_commands.hpp
 * @brief ACB command stream parsing helpers.
 *
 * CRI serializes command streams as big-endian TLV records:
 *   u16 command_code, u8 payload_size, payload bytes.
 *
 * The official runtime has two relevant interpreters: a broad serialized
 * action/event dispatcher and a compact runtime-parameter dispatcher used by
 * SequenceCommand, TrackCommand, and SynthCommand tables. The current
 * ACB reader only executes commands for cue-to-waveform traversal,
 * but the parser keeps every record visible so future
 * selector/action/MIDI work can build on the same byte model.
 */

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cricodecs::acb {

/**
 * CRI uses separate interpreters for track-event/action programs and
 * compact Sequence/Track/Synth parameter-pallet programs. Opcode values are
 * meaningful only within the interpreter selected by the containing table.
 */
enum class AcbCommandDispatcher : uint8_t {
    serialized_event,
    compact_parameter,
    /**
     * Older ACBs use one CommandTable for rows referenced as either track
     * events or compact Sequence/Track/Synth parameter programs.
     */
    legacy_shared,
};

enum class AcbCommandFamily : uint8_t {
    terminator,
    target_reference,
    timing,
    runtime_parameter,
    compact_runtime,
    category,
    cue_limit,
    bus_send,
    action,
    selector,
    midi,
    official_handled,
    unknown
};

enum class AcbCommandPayloadKind : uint8_t {
    none,
    u8,
    target_reference,
    be_u16,
    be_i16,
    be_u32,
    be_u16_pair,
    be_u16_pair_u8,
    be_f32,
    be_f32_pair,
    u8_pair,
    parameter_curve_7,
    parameter_curve_11,
    parameter_record_12,
    category_id_list,
    bus_name_send,
    sequence_wait_timer,
    raw,
    variable,
};

enum class AcbCommandTargetType : uint16_t {
    none = 0,
    waveform = 1,
    synth = 2,
    sequence = 3,
    outside_link = 5,
    direct_synth = 6,
    direct_sequence = 7,
    block_sequence = 8,
    direct_block_sequence = 9,
    special_11 = 11,
    special_12 = 12,
};

enum class AcbCommandCode : uint16_t {
    terminator = 0,

    compact_set_parameter_3 = 5,
    compact_curve_parameter_3 = 8,
    compact_set_parameter_5 = 11,
    compact_curve_parameter_5 = 12,
    compact_set_parameter_7 = 16,
    compact_parameter_curve_31 = 31,
    compact_parameter_pair_32 = 32,
    mute = 33,
    compact_set_parameter_145 = 34,
    compact_set_parameter_125_group = 36,
    compact_set_parameter_128 = 37,
    compact_set_parameter_129_group = 38,
    compact_set_parameter_132 = 39,
    compact_set_parameter_133_group = 40,
    compact_callback_limit = 43,
    compact_curve_parameter_3_timed = 60,
    compact_curve_parameter_5_timed = 61,
    compact_curve_parameter_6_timed = 63,
    compact_parameter_curve_64 = 64,
    category_information = 65,
    compact_set_parameter_98 = 66,
    compact_parameter_pair_85_86 = 67,
    compact_float_parameter_88 = 68,
    compact_float_parameter_89 = 69,
    compact_set_parameter_100 = 70,
    compact_curve_parameter_144 = 71,
    compact_add_runtime_counter = 72,
    compact_set_parameter_92 = 74,
    compact_set_parameter_93 = 75,
    compact_set_parameter_94 = 78,
    cue_limit_information = 79,
    compact_runtime_counter_80 = 80,
    compact_runtime_flag_81 = 81,
    compact_set_parameter_147 = 82,
    compact_set_parameter_11 = 83,
    compact_set_parameter_11_flag = 84,
    compact_set_parameter_12 = 85,
    compact_set_parameter_12_flag = 86,
    compact_set_parameter_0 = 87,
    compact_curve_parameter_0 = 88,
    compact_curve_parameter_0_range = 89,
    compact_set_parameter_160 = 90,
    compact_runtime_flag_93 = 93,
    sequence_wait_item = 98,
    selector_name = 99,
    selector_condition = 100,
    compact_parameter_pair_101 = 101,
    compact_runtime_flag_103 = 103,
    compact_set_parameter_96 = 108,
    compact_runtime_flag_110 = 110,
    bus_send_by_name = 111,
    compact_skip_112 = 112,
    compact_parameter_pair_113 = 113,
    compact_runtime_flag_114 = 114,
    compact_reset_parameter_152 = 117,
    compact_runtime_flag_118 = 118,
    sequence_wait_timer = 120,
    compact_set_parameter_4 = 121,
    compact_set_parameter_102 = 122,
    stop_at_loop_end = 124,
    compact_parameter_pair_125 = 125,
    compact_set_parameter_95 = 127,
    compact_set_parameter_97 = 128,
    compact_parameter_record_136 = 136,
    compact_float_parameter_146 = 146,
    compact_float_parameter_147 = 147,
    compact_float_pair_148 = 148,
    compact_unknown_161 = 161,

    sequence_start_milliseconds = 997,
    sequence_start_random = 998,
    sequence_start = 999,
    note_off = 996,
    sequence_callback_with_id = 1251,
    sequence_callback_with_string = 1252,
    sequence_callback_with_id_and_string = 1253,

    note_on = 2000,
    timing_control = 2001,
    set_synth_or_waveform = 2002,
    note_on_with_no = 2003,
    note_on_with_duration = 2004,
    timing_fraction = 2005,

    midi_event = 4000,
    transition_track = 4051,
    start_action_variant = 7099,
    start_action = 7100,
    stop_action = 7101,
    mute_action = 7102,
    set_selector_label = 7104,
    playback_parameter = 7107,
    pause_action = 7108,
    resume_action = 7109,
    stop_action_parameterized = 7110,
    pause_action_variant = 7111,
    resume_action_variant = 7112,
    set_next_block = 7113,
    action_7114 = 7114,
    set_selector_label_targeted = 7115,
};

struct AcbCommandDefinition {
    uint16_t code = 0;
    AcbCommandFamily family = AcbCommandFamily::unknown;
    AcbCommandPayloadKind payload_kind = AcbCommandPayloadKind::raw;
    std::string_view name = "unknown";
    std::optional<uint32_t> fixed_payload_size;
};

struct AcbCommandTarget {
    AcbCommandTargetType type = AcbCommandTargetType::none;
    uint16_t index = 0xFFFF;
};

struct AcbCommand {
    uint16_t code = 0;
    AcbCommandDispatcher dispatcher = AcbCommandDispatcher::serialized_event;
    AcbCommandFamily family = AcbCommandFamily::unknown;
    std::span<const uint8_t> payload;
};

[[nodiscard]] constexpr bool is_target_reference_command(uint16_t code) noexcept {
    return code == 2000 || code == 2003;
}

[[nodiscard]] constexpr AcbCommandDefinition command_definition(
    AcbCommandDispatcher dispatcher,
    uint16_t code
) noexcept {
    if (code == 0) {
        return {
            .code = code,
            .family = AcbCommandFamily::terminator,
            .payload_kind = AcbCommandPayloadKind::none,
            .name = "terminator",
            .fixed_payload_size = 0,
        };
    }

    if (dispatcher != AcbCommandDispatcher::compact_parameter &&
        is_target_reference_command(code)) {
        return {
            .code = code,
            .family = AcbCommandFamily::target_reference,
            .payload_kind = AcbCommandPayloadKind::target_reference,
            .name = code == 2000 ? "note_on" : "note_on_with_no",
            .fixed_payload_size = 4,
        };
    }

    if (dispatcher == AcbCommandDispatcher::compact_parameter ||
        (dispatcher == AcbCommandDispatcher::legacy_shared && code < 0x03E4)) {
        switch (code) {
        case 5:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_i16, "compact_set_parameter_3", 2};
        case 8:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_i16, "compact_curve_parameter_3", 2};
        case 11:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_i16, "compact_set_parameter_5", 2};
        case 12:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_i16, "compact_curve_parameter_5", 2};
        case 16:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_7", 2};
        case 31:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::parameter_curve_7, "compact_parameter_curve_31", 7};
        case 32:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16_pair, "compact_parameter_pair_32", 4};
        case 33:
            return {code, AcbCommandFamily::compact_runtime, AcbCommandPayloadKind::u8, "mute", 1};
        case 34:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_145", 2};
        case 36:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16_pair, "compact_set_parameter_125_group", 4};
        case 37:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16_pair, "compact_set_parameter_128", 4};
        case 38:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16_pair, "compact_set_parameter_129_group", 4};
        case 39:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_132", 2};
        case 40:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16_pair, "compact_set_parameter_133_group", 4};
        case 43:
            return {code, AcbCommandFamily::compact_runtime, AcbCommandPayloadKind::u8, "compact_callback_limit", 1};
        case 60:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16_pair, "compact_curve_parameter_3_timed", 4};
        case 61:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16_pair, "compact_curve_parameter_5_timed", 4};
        case 63:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16_pair, "compact_curve_parameter_6_timed", 4};
        case 64:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::parameter_curve_11, "compact_parameter_curve_64", 11};
        case 65:
            return {code, AcbCommandFamily::category, AcbCommandPayloadKind::category_id_list, "category_information", std::nullopt};
        case 66:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_98", 2};
        case 67:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16_pair, "compact_parameter_pair_85_86", 4};
        case 68:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_f32, "compact_float_parameter_88", 4};
        case 69:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_f32, "compact_float_parameter_89", 4};
        case 70:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::u8, "compact_set_parameter_100", 1};
        case 71:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16_pair, "compact_curve_parameter_144", 4};
        case 72:
            return {code, AcbCommandFamily::compact_runtime, AcbCommandPayloadKind::u8, "compact_add_runtime_counter", 1};
        case 74:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_92", 2};
        case 75:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_93", 2};
        case 78:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_94", 2};
        case 79:
            return {code, AcbCommandFamily::cue_limit, AcbCommandPayloadKind::be_u16_pair_u8, "cue_limit_information", 5};
        case 80:
            return {code, AcbCommandFamily::compact_runtime, AcbCommandPayloadKind::u8_pair, "compact_runtime_counter_80", 2};
        case 81:
            return {code, AcbCommandFamily::compact_runtime, AcbCommandPayloadKind::u8, "compact_runtime_flag_81", 1};
        case 82:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::u8, "compact_set_parameter_147", 1};
        case 83:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_11", 2};
        case 84:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_11_flag", 2};
        case 85:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_12", 2};
        case 86:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_12_flag", 2};
        case 87:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_0", 2};
        case 88:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_curve_parameter_0", 2};
        case 89:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16_pair, "compact_curve_parameter_0_range", 4};
        case 90:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_160", 2};
        case 93:
            return {code, AcbCommandFamily::compact_runtime, AcbCommandPayloadKind::u8, "compact_runtime_flag_93", 1};
        case 98:
            // CriSolv consumes this as a 10-byte sequence wait item. This is a
            // different command domain from serialized-event opcode 98.
            return {code, AcbCommandFamily::compact_runtime, AcbCommandPayloadKind::sequence_wait_timer, "sequence_wait_item", 10};
        case 99:
            return {code, AcbCommandFamily::selector, AcbCommandPayloadKind::be_u16, "selector_name", 2};
        case 100:
            return {code, AcbCommandFamily::selector, AcbCommandPayloadKind::be_u16_pair, "selector_condition", 4};
        case 101:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16_pair, "compact_parameter_pair_101", 4};
        case 103:
            return {code, AcbCommandFamily::compact_runtime, AcbCommandPayloadKind::u8, "compact_runtime_flag_103", 1};
        case 108:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_96", 2};
        case 110:
            return {code, AcbCommandFamily::compact_runtime, AcbCommandPayloadKind::u8, "compact_runtime_flag_110", 1};
        case 111:
            return {code, AcbCommandFamily::bus_send, AcbCommandPayloadKind::bus_name_send, "bus_send_by_name", 4};
        case 112:
            return {code, AcbCommandFamily::compact_runtime, AcbCommandPayloadKind::raw, "compact_skip_112", 4};
        case 113:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16_pair, "compact_parameter_pair_113", 4};
        case 114:
            return {code, AcbCommandFamily::compact_runtime, AcbCommandPayloadKind::u8, "compact_runtime_flag_114", 1};
        case 117:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::u8, "compact_reset_parameter_152", 1};
        case 118:
            return {code, AcbCommandFamily::compact_runtime, AcbCommandPayloadKind::u8, "compact_runtime_flag_118", 1};
        case 120:
            return {code, AcbCommandFamily::compact_runtime, AcbCommandPayloadKind::sequence_wait_timer, "sequence_wait_timer", 10};
        case 121:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_i16, "compact_set_parameter_4", 2};
        case 122:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::u8, "compact_set_parameter_102", 1};
        case 124:
            return {code, AcbCommandFamily::compact_runtime, AcbCommandPayloadKind::u8, "stop_at_loop_end", 1};
        case 125:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16_pair, "compact_parameter_pair_125", 4};
        case 127:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_95", 2};
        case 128:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_u16, "compact_set_parameter_97", 2};
        case 136:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::parameter_record_12, "compact_parameter_record_136", 12};
        case 146:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_f32, "compact_float_parameter_146", 4};
        case 147:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_f32, "compact_float_parameter_147", 4};
        case 148:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::be_f32_pair, "compact_float_pair_148", 8};
        case 161:
            // Seen as a zero-payload command in newer fixtures but absent from
            // the SDK 3.46 compact dispatcher. Preserve it version-neutrally.
            return {code, AcbCommandFamily::unknown, AcbCommandPayloadKind::none, "compact_command_161", 0};
        default:
            return {code, AcbCommandFamily::unknown, AcbCommandPayloadKind::raw, "unknown_compact", std::nullopt};
        }
    }

    switch (code) {
        case 0x03E4:
            return {code, AcbCommandFamily::timing, AcbCommandPayloadKind::variable, "note_off", std::nullopt};
        case 0x03E5:
            return {code, AcbCommandFamily::timing, AcbCommandPayloadKind::variable, "sequence_start_ms", std::nullopt};
        case 0x03E6:
            return {code, AcbCommandFamily::timing, AcbCommandPayloadKind::variable, "sequence_start_random", std::nullopt};
        case 0x03E7:
            return {code, AcbCommandFamily::timing, AcbCommandPayloadKind::variable, "sequence_start", std::nullopt};
        case 0x04AF:
            return {code, AcbCommandFamily::timing, AcbCommandPayloadKind::variable, "sequence_timing_1199", std::nullopt};
        case 0x04B0:
            return {code, AcbCommandFamily::timing, AcbCommandPayloadKind::variable, "sequence_timing_1200", std::nullopt};
        case 0x04B1:
            return {code, AcbCommandFamily::timing, AcbCommandPayloadKind::variable, "sequence_timing_list", std::nullopt};
        case 0x07D1:
            return {code, AcbCommandFamily::timing, AcbCommandPayloadKind::be_u32, "wait_milliseconds", 4};
        case 0x07D4:
        case 0x07D6:
        case 0x2430:
            return {code, AcbCommandFamily::timing, AcbCommandPayloadKind::variable, "timing_control", std::nullopt};
        case 0x07D5:
            return {code, AcbCommandFamily::timing, AcbCommandPayloadKind::be_i16, "wait_submillisecond_adjustment", 2};
        case 0x0062:
        case 0x04E2:
        case 0x04E3:
        case 0x04E4:
        case 0x04E5:
        case 0x1F40:
        case 0x1F41:
        case 0x1F42:
        case 0x1F43:
        case 0x1F44:
        case 0x1F45:
        case 0x1F46:
            return {code, AcbCommandFamily::runtime_parameter, AcbCommandPayloadKind::variable, "runtime_parameter", std::nullopt};
        case 0x07C5:
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::raw, "sequence_callback_with_id", std::nullopt};
        case 0x07C6:
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::raw, "sequence_callback_with_string", std::nullopt};
        case 0x07C7:
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::raw, "sequence_callback_with_id_and_string", std::nullopt};
        case 0x07D3:
        case 0x2401:
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::raw, "action", std::nullopt};
        case 0x0FA0:
            return {code, AcbCommandFamily::midi, AcbCommandPayloadKind::none, "midi_event_marker", 0};
        case 0x0FD2:
            // AtomCraft build_timed_acb_track_event emits this zero-payload
            // marker at the event end time. CriSolv then finalizes
            // the timed track event and related block-transition behavior.
            return {code, AcbCommandFamily::timing, AcbCommandPayloadKind::none, "end_track_event", 0};
        case 0x0FD3:
            return {code, AcbCommandFamily::timing, AcbCommandPayloadKind::raw, "transition_track", std::nullopt};
        case 7099:
            // Alternate AcOoActionStart encoding emitted by AtomCraft for a
            // resolved start target.
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::raw, "start_action_variant", std::nullopt};
        case 7100:
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::none, "start_action", 0};
        case 7101:
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::none, "stop_action", 0};
        case 7102:
            // AtomCraft AcOoActionMute. Payload variants are retained raw
            // until their fields are mapped across SDK versions.
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::raw, "mute_action", std::nullopt};
        case 7103:
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::raw, "action_7103", std::nullopt};
        case 7104:
            // AtomCraft AcOoActionSetSelectorLabel authoring serializer.
            return {code, AcbCommandFamily::selector, AcbCommandPayloadKind::be_u16_pair, "set_selector_label", 4};
        case 7105:
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::raw, "action_7105", std::nullopt};
        case 7106:
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::raw, "action_7106", std::nullopt};
        case 7107:
            // AtomCraft AcOoActionPlaybackParam authoring serializer. Every
            // local old/new fixture uses its 12-byte parameter/curve record.
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::parameter_record_12, "playback_parameter", 12};
        case 7108:
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::raw, "pause_action", std::nullopt};
        case 7109:
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::raw, "resume_action", std::nullopt};
        case 7110:
            // Alternate AcOoActionStop form. CriSolv reads u16/u8/u8; exact
            // submode terminology remains unknown.
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::raw, "stop_action_parameterized", 4};
        case 7113:
            // AtomCraft AcOoActionNextDestinationBlock authoring serializer.
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::be_u16, "set_next_block", 2};
        case 7111:
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::raw, "pause_action_variant", std::nullopt};
        case 7112:
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::raw, "resume_action_variant", std::nullopt};
        case 7114:
            return {code, AcbCommandFamily::action, AcbCommandPayloadKind::raw, "action_7114", std::nullopt};
        case 7115:
            // Newer target-addressed selector-label form, inferred from named
            // targets and StringValue rows in Music.acb.
            return {code, AcbCommandFamily::selector, AcbCommandPayloadKind::be_u16, "set_selector_label_targeted", 2};
        case 0x2328:
        case 0x2329:
        case 0x2400:
        case 0x2404:
        case 0x2406:
        case 0x2427:
            return {code, AcbCommandFamily::official_handled, AcbCommandPayloadKind::raw, "official_handled", std::nullopt};
        case 0x0F9F:
            return {code, AcbCommandFamily::midi, AcbCommandPayloadKind::raw, "midi", std::nullopt};
        default:
            break;
    }

    if (code >= 0x2500 && code <= 0x2565) {
        return {code, AcbCommandFamily::official_handled, AcbCommandPayloadKind::variable, "official_handled", std::nullopt};
    }

    return {code, AcbCommandFamily::unknown, AcbCommandPayloadKind::raw, "unknown", std::nullopt};
}

[[nodiscard]] constexpr AcbCommandFamily classify_command(
    AcbCommandDispatcher dispatcher,
    uint16_t code
) noexcept {
    return command_definition(dispatcher, code).family;
}

[[nodiscard]] std::string_view command_family_name(AcbCommandFamily family) noexcept;
[[nodiscard]] std::string_view command_payload_kind_name(AcbCommandPayloadKind kind) noexcept;
[[nodiscard]] constexpr std::string_view command_code_name(
    AcbCommandDispatcher dispatcher,
    uint16_t code
) noexcept {
    return command_definition(dispatcher, code).name;
}
[[nodiscard]] std::expected<std::vector<AcbCommand>, std::string> parse_command_stream(
    std::span<const uint8_t> data,
    AcbCommandDispatcher dispatcher = AcbCommandDispatcher::serialized_event);
[[nodiscard]] std::optional<AcbCommandTarget> command_target_reference(const AcbCommand& command) noexcept;

} // namespace cricodecs::acb
