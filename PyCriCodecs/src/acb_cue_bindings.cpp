#include "binding_helpers.hpp"

#include <nanobind/stl/optional.h>
#include <nanobind/stl/vector.h>

#include "../../CriCodecs/src/acb/acb_cue_resolver.hpp"
#include "../../CriCodecs/src/wav/wav_container.hpp"

namespace cricodecs::python {
namespace {

template <typename T>
void install_row_repr(
    nb::module_& module,
    const char* name,
    std::initializer_list<const char*> attrs) {
    static_cast<void>(sizeof(T));
    install_attr_repr(module, name, attrs);
}

[[nodiscard]] cricodecs::acb::AcbCueRenderOptions plan_audio_options(
    uint64_t hca_keycode,
    const nb::object& hca_subkey) {
    return {
        .hca_keycode = hca_keycode,
        .hca_subkey = hca_subkey.is_none()
            ? std::nullopt
            : std::optional<uint16_t>(nb::cast<uint16_t>(hca_subkey)),
    };
}

} // namespace

void bind_acb_cue_types(nb::module_& module) {
    using namespace cricodecs::acb;

    nb::enum_<AcbCommandDispatcher>(module, "AcbCommandDispatcher")
        .value("SERIALIZED_EVENT", AcbCommandDispatcher::serialized_event)
        .value("COMPACT_PARAMETER", AcbCommandDispatcher::compact_parameter)
        .value("LEGACY_SHARED", AcbCommandDispatcher::legacy_shared);
    nb::enum_<AcbCommandFamily>(module, "AcbCommandFamily")
        .value("TERMINATOR", AcbCommandFamily::terminator)
        .value("TARGET_REFERENCE", AcbCommandFamily::target_reference)
        .value("TIMING", AcbCommandFamily::timing)
        .value("RUNTIME_PARAMETER", AcbCommandFamily::runtime_parameter)
        .value("COMPACT_RUNTIME", AcbCommandFamily::compact_runtime)
        .value("CATEGORY", AcbCommandFamily::category)
        .value("CUE_LIMIT", AcbCommandFamily::cue_limit)
        .value("BUS_SEND", AcbCommandFamily::bus_send)
        .value("ACTION", AcbCommandFamily::action)
        .value("SELECTOR", AcbCommandFamily::selector)
        .value("MIDI", AcbCommandFamily::midi)
        .value("OFFICIAL_HANDLED", AcbCommandFamily::official_handled)
        .value("UNKNOWN", AcbCommandFamily::unknown);
    nb::enum_<AcbCommandTargetType>(module, "AcbCommandTargetType")
        .value("NONE", AcbCommandTargetType::none)
        .value("WAVEFORM", AcbCommandTargetType::waveform)
        .value("SYNTH", AcbCommandTargetType::synth)
        .value("SEQUENCE", AcbCommandTargetType::sequence)
        .value("OUTSIDE_LINK", AcbCommandTargetType::outside_link)
        .value("DIRECT_SYNTH", AcbCommandTargetType::direct_synth)
        .value("DIRECT_SEQUENCE", AcbCommandTargetType::direct_sequence)
        .value("BLOCK_SEQUENCE", AcbCommandTargetType::block_sequence)
        .value("DIRECT_BLOCK_SEQUENCE", AcbCommandTargetType::direct_block_sequence)
        .value("SPECIAL_11", AcbCommandTargetType::special_11)
        .value("SPECIAL_12", AcbCommandTargetType::special_12);
    nb::enum_<AcbCueCommandMeaning>(module, "AcbCueCommandMeaning")
        .value("UNKNOWN", AcbCueCommandMeaning::unknown)
        .value("TERMINATOR", AcbCueCommandMeaning::terminator)
        .value("TARGET_REFERENCE", AcbCueCommandMeaning::target_reference)
        .value("WAIT_MILLISECONDS", AcbCueCommandMeaning::wait_milliseconds)
        .value("WAIT_SUBMILLISECOND", AcbCueCommandMeaning::wait_submillisecond)
        .value("MUTE", AcbCueCommandMeaning::mute)
        .value("CATEGORY_INFORMATION", AcbCueCommandMeaning::category_information)
        .value("CUE_LIMIT_INFORMATION", AcbCueCommandMeaning::cue_limit_information)
        .value("RUNTIME_COUNTER_ADD", AcbCueCommandMeaning::runtime_counter_add)
        .value("RUNTIME_FLAG_WRITE", AcbCueCommandMeaning::runtime_flag_write)
        .value("SELECTOR_NAME", AcbCueCommandMeaning::selector_name)
        .value("SELECTOR_CONDITION", AcbCueCommandMeaning::selector_condition)
        .value("BUS_SEND_BY_NAME", AcbCueCommandMeaning::bus_send_by_name)
        .value("SEQUENCE_WAIT_ITEM", AcbCueCommandMeaning::sequence_wait_item)
        .value("SEQUENCE_WAIT_TIMER", AcbCueCommandMeaning::sequence_wait_timer)
        .value("STOP_AT_LOOP_END", AcbCueCommandMeaning::stop_at_loop_end)
        .value("SEQUENCE_START_MILLISECONDS", AcbCueCommandMeaning::sequence_start_milliseconds)
        .value("SEQUENCE_START_RANDOM", AcbCueCommandMeaning::sequence_start_random)
        .value("SEQUENCE_START", AcbCueCommandMeaning::sequence_start)
        .value("MIDI_EVENT", AcbCueCommandMeaning::midi_event)
        .value("END_TRACK_EVENT", AcbCueCommandMeaning::end_track_event)
        .value("START_ACTION", AcbCueCommandMeaning::start_action)
        .value("STOP_ACTION", AcbCueCommandMeaning::stop_action)
        .value("PAUSE_ACTION", AcbCueCommandMeaning::pause_action)
        .value("RESUME_ACTION", AcbCueCommandMeaning::resume_action)
        .value("STOP_ACTION_PARAMETERIZED", AcbCueCommandMeaning::stop_action_parameterized)
        .value("PLAYBACK_PARAMETER", AcbCueCommandMeaning::playback_parameter)
        .value("SET_NEXT_BLOCK", AcbCueCommandMeaning::set_next_block)
        .value("SET_SELECTOR_LABEL", AcbCueCommandMeaning::set_selector_label);
    nb::enum_<AcbCommandTableKind>(module, "AcbCommandTableKind")
        .value("TRACK_EVENT", AcbCommandTableKind::track_event)
        .value("LEGACY_COMMAND", AcbCommandTableKind::legacy_command)
        .value("SEQUENCE_COMMAND", AcbCommandTableKind::sequence_command)
        .value("TRACK_COMMAND", AcbCommandTableKind::track_command)
        .value("SYNTH_COMMAND", AcbCommandTableKind::synth_command);
    nb::enum_<AcbCueNodeKind>(module, "AcbCueNodeKind")
        .value("CUE", AcbCueNodeKind::cue)
        .value("WAVEFORM", AcbCueNodeKind::waveform)
        .value("SYNTH", AcbCueNodeKind::synth)
        .value("SEQUENCE", AcbCueNodeKind::sequence)
        .value("TRACK", AcbCueNodeKind::track)
        .value("ACTION_TRACK", AcbCueNodeKind::action_track)
        .value("TRACK_EVENT", AcbCueNodeKind::track_event)
        .value("LEGACY_COMMAND", AcbCueNodeKind::legacy_command)
        .value("SEQUENCE_COMMAND", AcbCueNodeKind::sequence_command)
        .value("TRACK_COMMAND", AcbCueNodeKind::track_command)
        .value("SYNTH_COMMAND", AcbCueNodeKind::synth_command)
        .value("BLOCK_SEQUENCE", AcbCueNodeKind::block_sequence)
        .value("BLOCK", AcbCueNodeKind::block)
        .value("OUTSIDE_LINK", AcbCueNodeKind::outside_link);
    nb::enum_<AcbCueEdgeKind>(module, "AcbCueEdgeKind")
        .value("CUE_REFERENCE", AcbCueEdgeKind::cue_reference)
        .value("SYNTH_REFERENCE", AcbCueEdgeKind::synth_reference)
        .value("TRACK", AcbCueEdgeKind::track)
        .value("ACTION_TRACK", AcbCueEdgeKind::action_track)
        .value("BLOCK", AcbCueEdgeKind::block)
        .value("EVENT_STREAM", AcbCueEdgeKind::event_stream)
        .value("PARAMETER_STREAM", AcbCueEdgeKind::parameter_stream)
        .value("COMMAND_TARGET", AcbCueEdgeKind::command_target)
        .value("OUTSIDE_LINK", AcbCueEdgeKind::outside_link);
    nb::enum_<AcbWaveformNameView>(module, "AcbWaveformNameView")
        .value("PREFERRED", AcbWaveformNameView::preferred)
        .value("EXACT_WAVEFORM", AcbWaveformNameView::exact_waveform)
        .value("ASSOCIATED_AWB_ASSET", AcbWaveformNameView::associated_awb_asset);
    nb::enum_<AcbCueChoiceDomain>(module, "AcbCueChoiceDomain")
        .value("SEQUENCE_TRACK", AcbCueChoiceDomain::sequence_track)
        .value("SYNTH_REFERENCE", AcbCueChoiceDomain::synth_reference);
    nb::enum_<AcbCueAwbBank>(module, "AcbCueAwbBank")
        .value("MEMORY", AcbCueAwbBank::memory)
        .value("STREAM", AcbCueAwbBank::stream);

    nb::class_<AcbCommandTarget>(module, "AcbCommandTarget")
        .def_ro("type", &AcbCommandTarget::type)
        .def_ro("index", &AcbCommandTarget::index);
    nb::class_<AcbCueCommand>(module, "AcbCueCommand")
        .def_ro("code", &AcbCueCommand::code)
        .def_ro("dispatcher", &AcbCueCommand::dispatcher)
        .def_ro("family", &AcbCueCommand::family)
        .def_ro("meaning", &AcbCueCommand::meaning)
        .def_prop_ro("payload", [](const AcbCueCommand& self) {
            return to_python_bytes(self.payload);
        })
        .def_ro("target", &AcbCueCommand::target)
        .def_ro("argument_u16", &AcbCueCommand::argument_u16)
        .def_ro("argument_u16_2", &AcbCueCommand::argument_u16_2)
        .def_ro("argument_i16", &AcbCueCommand::argument_i16)
        .def_ro("time_advance_us", &AcbCueCommand::time_advance_us);
    nb::class_<AcbScheduledTarget>(module, "AcbScheduledTarget")
        .def_ro("command_index", &AcbScheduledTarget::command_index)
        .def_ro("time_us", &AcbScheduledTarget::time_us)
        .def_ro("target", &AcbScheduledTarget::target);
    nb::class_<AcbCueCommandStream>(module, "AcbCueCommandStream")
        .def_ro("row_index", &AcbCueCommandStream::row_index)
        .def_ro("table_kind", &AcbCueCommandStream::table_kind)
        .def_ro("dispatcher", &AcbCueCommandStream::dispatcher)
        .def_prop_ro("raw", [](const AcbCueCommandStream& self) {
            return to_python_bytes(self.raw);
        })
        .def_ro("commands", &AcbCueCommandStream::commands)
        .def_ro("scheduled_targets", &AcbCueCommandStream::scheduled_targets)
        .def_ro("duration_us", &AcbCueCommandStream::duration_us);

    nb::class_<AcbCueName>(module, "AcbCueName")
        .def_ro("row_index", &AcbCueName::row_index)
        .def_ro("cue_index", &AcbCueName::cue_index)
        .def_ro("name", &AcbCueName::name)
        .def_prop_ro("name_raw", [](const AcbCueName& self) {
            return string_to_python_bytes(self.name_raw);
        });
    nb::class_<AcbCueReference>(module, "AcbCueReference")
        .def_ro("type", &AcbCueReference::type)
        .def_prop_ro("type_name", [](const AcbCueReference& self) {
            return std::string(reference_type_name(self.type));
        })
        .def_ro("index", &AcbCueReference::index);
    nb::class_<AcbCue>(module, "AcbCue")
        .def_ro("row_index", &AcbCue::row_index)
        .def_ro("cue_id", &AcbCue::cue_id)
        .def_ro("reference", &AcbCue::reference)
        .def_ro("name_rows", &AcbCue::name_rows)
        .def_ro("length", &AcbCue::length)
        .def_ro("worksize", &AcbCue::worksize)
        .def_ro("num_related_waveforms", &AcbCue::num_related_waveforms)
        .def_ro("header_visibility", &AcbCue::header_visibility);
    nb::class_<AcbSynthReference>(module, "AcbSynthReference")
        .def_ro("type", &AcbSynthReference::type)
        .def_prop_ro("type_name", [](const AcbSynthReference& self) {
            return std::string(reference_type_name(self.type));
        })
        .def_ro("index", &AcbSynthReference::index);
    nb::class_<AcbSynth>(module, "AcbSynth")
        .def_ro("row_index", &AcbSynth::row_index)
        .def_ro("type", &AcbSynth::type)
        .def_ro("command_index", &AcbSynth::command_index)
        .def_ro("action_track_start_index", &AcbSynth::action_track_start_index)
        .def_ro("num_action_tracks", &AcbSynth::num_action_tracks)
        .def_prop_ro("reference_items_raw", [](const AcbSynth& self) {
            return to_python_bytes(self.reference_items_raw);
        })
        .def_ro("reference_items", &AcbSynth::reference_items)
        .def_prop_ro("track_values_raw", [](const AcbSynth& self) {
            return to_python_bytes(self.track_values_raw);
        })
        .def_ro("track_values", &AcbSynth::track_values);
    nb::class_<AcbTrack>(module, "AcbTrack")
        .def_ro("row_index", &AcbTrack::row_index)
        .def_ro("event_index", &AcbTrack::event_index)
        .def_ro("command_index", &AcbTrack::command_index)
        .def_ro("target_type", &AcbTrack::target_type)
        .def_ro("target_name", &AcbTrack::target_name)
        .def_prop_ro("target_name_raw", [](const AcbTrack& self) {
            return string_to_python_bytes(self.target_name_raw);
        })
        .def_ro("target_id", &AcbTrack::target_id)
        .def_ro("target_acb_name", &AcbTrack::target_acb_name)
        .def_prop_ro("target_acb_name_raw", [](const AcbTrack& self) {
            return string_to_python_bytes(self.target_acb_name_raw);
        })
        .def_ro("scope", &AcbTrack::scope)
        .def_ro("target_track_no", &AcbTrack::target_track_no);
    nb::class_<AcbSequence>(module, "AcbSequence")
        .def_ro("row_index", &AcbSequence::row_index)
        .def_ro("type", &AcbSequence::type)
        .def_prop_ro("type_name", [](const AcbSequence& self) {
            return std::string(sequence_type_name(self.type));
        })
        .def_ro("playback_ratio", &AcbSequence::playback_ratio)
        .def_ro("command_index", &AcbSequence::command_index)
        .def_ro("track_indices", &AcbSequence::track_indices)
        .def_prop_ro("track_values_raw", [](const AcbSequence& self) {
            return to_python_bytes(self.track_values_raw);
        })
        .def_ro("track_values", &AcbSequence::track_values)
        .def_ro("action_track_start_index", &AcbSequence::action_track_start_index)
        .def_ro("num_action_tracks", &AcbSequence::num_action_tracks)
        .def_ro("watch_action_start_index", &AcbSequence::watch_action_start_index)
        .def_ro("num_watch_actions", &AcbSequence::num_watch_actions)
        .def_ro("stop_action_start_index", &AcbSequence::stop_action_start_index)
        .def_ro("num_stop_actions", &AcbSequence::num_stop_actions);
    nb::class_<AcbBlock>(module, "AcbBlock")
        .def_ro("row_index", &AcbBlock::row_index)
        .def_ro("track_indices", &AcbBlock::track_indices)
        .def_ro("playback_type", &AcbBlock::playback_type)
        .def_ro("num_loops", &AcbBlock::num_loops)
        .def_ro("transition_timing", &AcbBlock::transition_timing)
        .def_ro("transition_timing_value", &AcbBlock::transition_timing_value)
        .def_ro("jump_previous_behavior", &AcbBlock::jump_previous_behavior)
        .def_ro("jump_destination", &AcbBlock::jump_destination)
        .def_ro("name_index", &AcbBlock::name_index)
        .def_ro("length_ms", &AcbBlock::length_ms)
        .def_ro("length_submillisecond", &AcbBlock::length_submillisecond)
        .def_prop_ro("duration_us", &AcbBlock::duration_us)
        .def_ro("start_position_ms", &AcbBlock::start_position_ms)
        .def_ro("start_position_submillisecond", &AcbBlock::start_position_submillisecond)
        .def_prop_ro("start_position_us", &AcbBlock::start_position_us)
        .def_ro("action_track_start_index", &AcbBlock::action_track_start_index)
        .def_ro("num_action_tracks", &AcbBlock::num_action_tracks)
        .def_ro("destination_blocks", &AcbBlock::destination_blocks)
        .def_prop_ro("destination_values_raw", [](const AcbBlock& self) {
            return to_python_bytes(self.destination_values_raw);
        });
    nb::class_<AcbBlockSequence>(module, "AcbBlockSequence")
        .def_ro("row_index", &AcbBlockSequence::row_index)
        .def_ro("type", &AcbBlockSequence::type)
        .def_prop_ro("type_name", [](const AcbBlockSequence& self) {
            return std::string(sequence_type_name(self.type));
        })
        .def_ro("playback_ratio", &AcbBlockSequence::playback_ratio)
        .def_ro("command_index", &AcbBlockSequence::command_index)
        .def_ro("track_indices", &AcbBlockSequence::track_indices)
        .def_ro("block_indices", &AcbBlockSequence::block_indices)
        .def_prop_ro("track_values_raw", [](const AcbBlockSequence& self) {
            return to_python_bytes(self.track_values_raw);
        })
        .def_ro("track_values", &AcbBlockSequence::track_values)
        .def_ro("watch_action_start_index", &AcbBlockSequence::watch_action_start_index)
        .def_ro("num_watch_actions", &AcbBlockSequence::num_watch_actions)
        .def_ro("stop_action_start_index", &AcbBlockSequence::stop_action_start_index)
        .def_ro("num_stop_actions", &AcbBlockSequence::num_stop_actions);
    nb::class_<AcbCueWaveform>(module, "AcbCueWaveform")
        .def_ro("row_index", &AcbCueWaveform::row_index)
        .def_ro("id", &AcbCueWaveform::id)
        .def_ro("memory_awb_id", &AcbCueWaveform::memory_awb_id)
        .def_ro("stream_awb_id", &AcbCueWaveform::stream_awb_id)
        .def_ro("stream_awb_port_no", &AcbCueWaveform::stream_awb_port_no)
        .def_ro("streaming", &AcbCueWaveform::streaming)
        .def_ro("encode_type", &AcbCueWaveform::encode_type)
        .def_ro("num_channels", &AcbCueWaveform::num_channels)
        .def_ro("loop_flag", &AcbCueWaveform::loop_flag)
        .def_ro("sampling_rate", &AcbCueWaveform::sampling_rate)
        .def_ro("num_samples", &AcbCueWaveform::num_samples)
        .def_ro("extension_data", &AcbCueWaveform::extension_data);
    nb::class_<AcbWaveformExtension>(module, "AcbWaveformExtension")
        .def_ro("row_index", &AcbWaveformExtension::row_index)
        .def_ro("loop_start", &AcbWaveformExtension::loop_start)
        .def_ro("loop_end", &AcbWaveformExtension::loop_end);
    nb::class_<AcbStringValue>(module, "AcbStringValue")
        .def_ro("row_index", &AcbStringValue::row_index)
        .def_ro("value", &AcbStringValue::value)
        .def_prop_ro("value_raw", [](const AcbStringValue& self) {
            return string_to_python_bytes(self.value_raw);
        });
    nb::class_<AcbOutsideLink>(module, "AcbOutsideLink")
        .def_ro("row_index", &AcbOutsideLink::row_index)
        .def_ro("cue_id", &AcbOutsideLink::cue_id)
        .def_ro("cue_name_string_index", &AcbOutsideLink::cue_name_string_index)
        .def_ro("acb_name_string_index", &AcbOutsideLink::acb_name_string_index);
    nb::class_<AcbCueNode>(module, "AcbCueNode")
        .def_ro("kind", &AcbCueNode::kind)
        .def_ro("index", &AcbCueNode::index);
    nb::class_<AcbCueEdge>(module, "AcbCueEdge")
        .def_ro("from_node", &AcbCueEdge::from)
        .def_ro("to_node", &AcbCueEdge::to)
        .def_ro("kind", &AcbCueEdge::kind)
        .def_ro("ordinal", &AcbCueEdge::ordinal);
    nb::class_<AcbUnresolvedReference>(module, "AcbUnresolvedReference")
        .def_ro("source", &AcbUnresolvedReference::source)
        .def_ro("type", &AcbUnresolvedReference::type)
        .def_ro("index", &AcbUnresolvedReference::index)
        .def_ro("reason", &AcbUnresolvedReference::reason);
    nb::class_<AcbCueAssembly>(module, "AcbCueAssembly")
        .def_ro("cue_index", &AcbCueAssembly::cue_index)
        .def_ro("nodes", &AcbCueAssembly::nodes)
        .def_ro("edges", &AcbCueAssembly::edges)
        .def_ro("unresolved", &AcbCueAssembly::unresolved)
        .def_ro("has_cycle", &AcbCueAssembly::has_cycle);
    nb::class_<AcbWaveformCueView>(module, "AcbWaveformCueView")
        .def_ro("waveform_index", &AcbWaveformCueView::waveform_index)
        .def_ro("exact_cue_name_rows", &AcbWaveformCueView::exact_cue_name_rows)
        .def_ro("preferred_cue_name_rows", &AcbWaveformCueView::preferred_cue_name_rows)
        .def_ro("associated_awb_cue_name_rows", &AcbWaveformCueView::associated_awb_cue_name_rows);

    nb::class_<AcbCueClipPlan>(module, "AcbCueClipPlan")
        .def_ro("waveform_index", &AcbCueClipPlan::waveform_index)
        .def_ro("start_time_us", &AcbCueClipPlan::start_time_us)
        .def_ro("awb_wave_id", &AcbCueClipPlan::awb_wave_id)
        .def_ro("awb_stream_index", &AcbCueClipPlan::awb_stream_index)
        .def_ro("awb_bank", &AcbCueClipPlan::awb_bank);
    nb::class_<AcbCueBlockPlan>(module, "AcbCueBlockPlan")
        .def_ro("block_position", &AcbCueBlockPlan::block_position)
        .def_ro("block_index", &AcbCueBlockPlan::block_index)
        .def_ro("name", &AcbCueBlockPlan::name)
        .def_ro("duration_us", &AcbCueBlockPlan::duration_us)
        .def_ro("authored_loop_count", &AcbCueBlockPlan::authored_loop_count)
        .def_ro("render_loop_count", &AcbCueBlockPlan::render_loop_count)
        .def_ro("forced_advance", &AcbCueBlockPlan::forced_advance)
        .def_ro("skipped_empty_hold", &AcbCueBlockPlan::skipped_empty_hold)
        .def_ro("clips", &AcbCueBlockPlan::clips);
    nb::class_<AcbCuePlaybackPlan>(module, "AcbCuePlaybackPlan")
        .def_ro("cue_index", &AcbCuePlaybackPlan::cue_index)
        .def_ro("cue_id", &AcbCuePlaybackPlan::cue_id)
        .def_ro("cue_name", &AcbCuePlaybackPlan::cue_name)
        .def_ro("blocks", &AcbCuePlaybackPlan::blocks)
        .def_prop_ro("block_count", [](const AcbCuePlaybackPlan& self) {
            return self.blocks.size();
        })
        .def(
            "wav_bytes",
            [](const AcbCuePlaybackPlan& self,
               const AcbContainer& acb,
               uint64_t hca_keycode,
               const nb::object& hca_subkey) {
                auto rendered = unwrap_expected(render_cue_plan(
                    acb, self, plan_audio_options(hca_keycode, hca_subkey)));
                return to_python_bytes(unwrap_expected(
                    wav::WavContainer::build_bytes(
                        rendered.pcm,
                        rendered.sample_rate,
                        rendered.channels)));
            },
            nb::arg("acb"),
            nb::arg("hca_keycode") = 0,
            nb::arg("hca_subkey") = nb::none())
        .def(
            "export",
            [](const AcbCuePlaybackPlan& self,
               const AcbContainer& acb,
               const nb::object& output_path,
               uint64_t hca_keycode,
               const nb::object& hca_subkey) {
                unwrap_expected(extract_cue_plan(
                    acb,
                    self,
                    require_python_path(output_path, "output_path"),
                    plan_audio_options(hca_keycode, hca_subkey)));
            },
            nb::arg("acb"),
            nb::arg("output_path"),
            nb::arg("hca_keycode") = 0,
            nb::arg("hca_subkey") = nb::none());

    nb::class_<AcbCueChoiceSelection>(module, "AcbCueChoiceSelection")
        .def(nb::init<>())
        .def_rw("domain", &AcbCueChoiceSelection::domain)
        .def_rw("node_index", &AcbCueChoiceSelection::node_index)
        .def_rw("occurrence", &AcbCueChoiceSelection::occurrence)
        .def_rw("option_index", &AcbCueChoiceSelection::option_index)
        .def_rw("mode", &AcbCueChoiceSelection::mode)
        .def_rw("selector_name", &AcbCueChoiceSelection::selector_name)
        .def_rw("selector_value", &AcbCueChoiceSelection::selector_value);
    nb::class_<AcbCuePlanVariant>(module, "AcbCuePlanVariant")
        .def_ro("plan", &AcbCuePlanVariant::plan)
        .def_ro("paths", &AcbCuePlanVariant::paths);
    nb::class_<AcbCueTerminalPath>(module, "AcbCueTerminalPath")
        .def_ro("choices", &AcbCueTerminalPath::choices)
        .def_ro("error", &AcbCueTerminalPath::error);
    nb::class_<AcbCuePlanEnumeration>(module, "AcbCuePlanEnumeration")
        .def_ro("cue_index", &AcbCuePlanEnumeration::cue_index)
        .def_ro("variants", &AcbCuePlanEnumeration::variants)
        .def_ro("terminal_errors", &AcbCuePlanEnumeration::terminal_errors)
        .def_ro("terminal_paths", &AcbCuePlanEnumeration::terminal_paths)
        .def_ro("explored_paths", &AcbCuePlanEnumeration::explored_paths);
    nb::class_<AcbCueSelectorValue>(module, "AcbCueSelectorValue")
        .def(nb::init<>())
        .def_rw("name", &AcbCueSelectorValue::name)
        .def_rw("value", &AcbCueSelectorValue::value);
    nb::class_<AcbCuePlanSource>(module, "AcbCuePlanSource")
        .def_ro("source_cue_index", &AcbCuePlanSource::source_cue_index)
        .def_ro("source_cue_id", &AcbCuePlanSource::source_cue_id)
        .def_ro("source_cue_name", &AcbCuePlanSource::source_cue_name)
        .def_ro("terminal_cue_index", &AcbCuePlanSource::terminal_cue_index)
        .def_ro("action_cue_chain", &AcbCuePlanSource::action_cue_chain)
        .def_ro("selector_values", &AcbCuePlanSource::selector_values)
        .def_ro("paths", &AcbCuePlanSource::paths);
    nb::class_<AcbResolvedCuePlan>(module, "AcbResolvedCuePlan")
        .def_ro("plan", &AcbResolvedCuePlan::plan)
        .def_ro("sources", &AcbResolvedCuePlan::sources);
    nb::class_<AcbCueSheetResolution>(module, "AcbCueSheetResolution")
        .def_ro("plans", &AcbCueSheetResolution::plans)
        .def_prop_ro("plan_count", [](const AcbCueSheetResolution& self) {
            return self.plans.size();
        })
        .def_ro("non_playable_cues", &AcbCueSheetResolution::non_playable_cues)
        .def(
            "filenames",
            &cue_plan_filenames,
            nb::arg("include_index_prefix") = true);

    nb::class_<AcbCueGraph>(module, "AcbCueGraph")
        .def_prop_ro("cues", &AcbCueGraph::cues)
        .def_prop_ro("cue_names", &AcbCueGraph::cue_names)
        .def_prop_ro("synths", &AcbCueGraph::synths)
        .def_prop_ro("sequences", &AcbCueGraph::sequences)
        .def_prop_ro("tracks", &AcbCueGraph::tracks)
        .def_prop_ro("action_tracks", &AcbCueGraph::action_tracks)
        .def_prop_ro("block_sequences", &AcbCueGraph::block_sequences)
        .def_prop_ro("blocks", &AcbCueGraph::blocks)
        .def_prop_ro("waveforms", &AcbCueGraph::waveforms)
        .def_prop_ro("waveform_extensions", &AcbCueGraph::waveform_extensions)
        .def_prop_ro("strings", &AcbCueGraph::strings)
        .def_prop_ro("outside_links", &AcbCueGraph::outside_links)
        .def_prop_ro("track_events", &AcbCueGraph::track_events)
        .def_prop_ro("legacy_commands", &AcbCueGraph::legacy_commands)
        .def_prop_ro("sequence_commands", &AcbCueGraph::sequence_commands)
        .def_prop_ro("track_commands", &AcbCueGraph::track_commands)
        .def_prop_ro("synth_commands", &AcbCueGraph::synth_commands)
        .def_prop_ro("has_embedded_awb", &AcbCueGraph::has_embedded_awb)
        .def_prop_ro("cue_count", [](const AcbCueGraph& self) {
            return self.cues().size();
        })
        .def_prop_ro("waveform_count", [](const AcbCueGraph& self) {
            return self.waveforms().size();
        })
        .def(
            "cue",
            [](const AcbCueGraph& self, uint32_t index) -> const AcbCue& {
                if (index >= self.cues().size()) {
                    raise_value_error("ACB cue index is out of range");
                }
                return self.cues()[index];
            },
            nb::arg("index"),
            nb::rv_policy::reference_internal)
        .def(
            "cue_index_by_id",
            [](const AcbCueGraph& self, uint32_t cue_id) -> nb::object {
                const auto* cue = self.cue_by_id(cue_id);
                if (cue == nullptr) {
                    return nb::none();
                }
                return nb::cast(static_cast<uint32_t>(
                    cue - self.cues().data()));
            },
            nb::arg("cue_id"))
        .def("cue_name", [](const AcbCueGraph& self, uint32_t index) {
            if (index >= self.cues().size()) {
                raise_value_error("ACB cue index is out of range");
            }
            return std::string(self.cue_name(index));
        }, nb::arg("index"))
        .def("string_value", [](const AcbCueGraph& self, uint32_t index) {
            return std::string(self.string_value(index));
        }, nb::arg("index"))
        .def("assemble_cue", [](const AcbCueGraph& self, uint32_t index) {
            return unwrap_expected(self.assemble_cue(index));
        }, nb::arg("index"))
        .def("waveform_cue_views", [](const AcbCueGraph& self) {
            return unwrap_expected(self.waveform_cue_views());
        })
        .def("waveform_name", [](const AcbCueGraph& self,
                                 uint32_t index,
                                 AcbWaveformNameView view,
                                 bool raw) {
            return unwrap_expected(self.waveform_name(index, view, raw));
        },
        nb::arg("index"),
        nb::arg("view") = AcbWaveformNameView::preferred,
        nb::arg("raw") = false)
        .def("waveform_names", [](const AcbCueGraph& self,
                                  AcbWaveformNameView view,
                                  bool raw) {
            return unwrap_expected(self.waveform_names(view, raw));
        },
        nb::arg("view") = AcbWaveformNameView::preferred,
        nb::arg("raw") = false);

    install_row_repr<AcbCommandTarget>(module, "AcbCommandTarget", {"type", "index"});
    install_row_repr<AcbCueCommand>(module, "AcbCueCommand", {"code", "meaning", "payload"});
    install_row_repr<AcbCueCommandStream>(module, "AcbCueCommandStream", {"row_index", "table_kind", "duration_us"});
    install_row_repr<AcbCueName>(module, "AcbCueName", {"row_index", "cue_index", "name"});
    install_row_repr<AcbCueReference>(module, "AcbCueReference", {"type", "type_name", "index"});
    install_row_repr<AcbCue>(module, "AcbCue", {"row_index", "cue_id", "reference", "name_rows"});
    install_row_repr<AcbSynth>(module, "AcbSynth", {"row_index", "type", "command_index", "reference_items"});
    install_row_repr<AcbTrack>(module, "AcbTrack", {"row_index", "event_index", "command_index", "target_name", "target_id"});
    install_row_repr<AcbSequence>(module, "AcbSequence", {"row_index", "type", "type_name", "track_indices"});
    install_row_repr<AcbBlock>(module, "AcbBlock", {"row_index", "num_loops", "duration_us", "track_indices"});
    install_row_repr<AcbBlockSequence>(module, "AcbBlockSequence", {"row_index", "type", "type_name", "block_indices"});
    install_row_repr<AcbCueWaveform>(module, "AcbCueWaveform", {"row_index", "id", "memory_awb_id", "stream_awb_id", "encode_type"});
    install_row_repr<AcbCueNode>(module, "AcbCueNode", {"kind", "index"});
    install_row_repr<AcbCueEdge>(module, "AcbCueEdge", {"from_node", "to_node", "kind", "ordinal"});
    install_row_repr<AcbCueAssembly>(module, "AcbCueAssembly", {"cue_index", "has_cycle", "nodes", "edges", "unresolved"});
    install_row_repr<AcbCueClipPlan>(module, "AcbCueClipPlan", {"waveform_index", "start_time_us", "awb_wave_id", "awb_stream_index", "awb_bank"});
    install_row_repr<AcbCueBlockPlan>(module, "AcbCueBlockPlan", {"block_position", "block_index", "name", "duration_us", "authored_loop_count", "render_loop_count"});
    install_row_repr<AcbCuePlaybackPlan>(module, "AcbCuePlaybackPlan", {"cue_index", "cue_id", "cue_name", "block_count"});
    install_row_repr<AcbCueChoiceSelection>(module, "AcbCueChoiceSelection", {"domain", "node_index", "occurrence", "option_index", "selector_name", "selector_value"});
    install_row_repr<AcbCuePlanSource>(module, "AcbCuePlanSource", {"source_cue_index", "source_cue_id", "source_cue_name", "terminal_cue_index", "selector_values"});
    install_row_repr<AcbResolvedCuePlan>(module, "AcbResolvedCuePlan", {"plan", "sources"});
    install_row_repr<AcbCueSheetResolution>(module, "AcbCueSheetResolution", {"plan_count", "non_playable_cues"});
    install_row_repr<AcbCueGraph>(module, "AcbCueGraph", {"cue_count", "waveform_count", "has_embedded_awb"});
}

} // namespace cricodecs::python
