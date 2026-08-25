from enum import Enum
from typing import Any

from .awb import AacEncryptionState, Awb, AwbInfo, KeyRecoveryResult

class AcbCommandDispatcher(Enum):
    SERIALIZED_EVENT: AcbCommandDispatcher
    COMPACT_PARAMETER: AcbCommandDispatcher
    LEGACY_SHARED: AcbCommandDispatcher

class AcbCommandFamily(Enum):
    TERMINATOR: AcbCommandFamily
    TARGET_REFERENCE: AcbCommandFamily
    TIMING: AcbCommandFamily
    RUNTIME_PARAMETER: AcbCommandFamily
    COMPACT_RUNTIME: AcbCommandFamily
    CATEGORY: AcbCommandFamily
    CUE_LIMIT: AcbCommandFamily
    BUS_SEND: AcbCommandFamily
    ACTION: AcbCommandFamily
    SELECTOR: AcbCommandFamily
    MIDI: AcbCommandFamily
    OFFICIAL_HANDLED: AcbCommandFamily
    UNKNOWN: AcbCommandFamily

class AcbCommandTargetType(Enum):
    NONE: AcbCommandTargetType
    WAVEFORM: AcbCommandTargetType
    SYNTH: AcbCommandTargetType
    SEQUENCE: AcbCommandTargetType
    OUTSIDE_LINK: AcbCommandTargetType
    DIRECT_SYNTH: AcbCommandTargetType
    DIRECT_SEQUENCE: AcbCommandTargetType
    BLOCK_SEQUENCE: AcbCommandTargetType
    DIRECT_BLOCK_SEQUENCE: AcbCommandTargetType
    SPECIAL_11: AcbCommandTargetType
    SPECIAL_12: AcbCommandTargetType

class AcbCueCommandMeaning(Enum):
    UNKNOWN: AcbCueCommandMeaning
    TERMINATOR: AcbCueCommandMeaning
    TARGET_REFERENCE: AcbCueCommandMeaning
    WAIT_MILLISECONDS: AcbCueCommandMeaning
    WAIT_SUBMILLISECOND: AcbCueCommandMeaning
    MUTE: AcbCueCommandMeaning
    CATEGORY_INFORMATION: AcbCueCommandMeaning
    CUE_LIMIT_INFORMATION: AcbCueCommandMeaning
    RUNTIME_COUNTER_ADD: AcbCueCommandMeaning
    RUNTIME_FLAG_WRITE: AcbCueCommandMeaning
    SELECTOR_NAME: AcbCueCommandMeaning
    SELECTOR_CONDITION: AcbCueCommandMeaning
    BUS_SEND_BY_NAME: AcbCueCommandMeaning
    SEQUENCE_WAIT_ITEM: AcbCueCommandMeaning
    SEQUENCE_WAIT_TIMER: AcbCueCommandMeaning
    STOP_AT_LOOP_END: AcbCueCommandMeaning
    SEQUENCE_START_MILLISECONDS: AcbCueCommandMeaning
    SEQUENCE_START_RANDOM: AcbCueCommandMeaning
    SEQUENCE_START: AcbCueCommandMeaning
    MIDI_EVENT: AcbCueCommandMeaning
    END_TRACK_EVENT: AcbCueCommandMeaning
    START_ACTION: AcbCueCommandMeaning
    STOP_ACTION: AcbCueCommandMeaning
    PAUSE_ACTION: AcbCueCommandMeaning
    RESUME_ACTION: AcbCueCommandMeaning
    STOP_ACTION_PARAMETERIZED: AcbCueCommandMeaning
    PLAYBACK_PARAMETER: AcbCueCommandMeaning
    SET_NEXT_BLOCK: AcbCueCommandMeaning
    SET_SELECTOR_LABEL: AcbCueCommandMeaning

class AcbCommandTableKind(Enum):
    TRACK_EVENT: AcbCommandTableKind
    LEGACY_COMMAND: AcbCommandTableKind
    SEQUENCE_COMMAND: AcbCommandTableKind
    TRACK_COMMAND: AcbCommandTableKind
    SYNTH_COMMAND: AcbCommandTableKind

class AcbCueNodeKind(Enum):
    CUE: AcbCueNodeKind
    WAVEFORM: AcbCueNodeKind
    SYNTH: AcbCueNodeKind
    SEQUENCE: AcbCueNodeKind
    TRACK: AcbCueNodeKind
    ACTION_TRACK: AcbCueNodeKind
    TRACK_EVENT: AcbCueNodeKind
    LEGACY_COMMAND: AcbCueNodeKind
    SEQUENCE_COMMAND: AcbCueNodeKind
    TRACK_COMMAND: AcbCueNodeKind
    SYNTH_COMMAND: AcbCueNodeKind
    BLOCK_SEQUENCE: AcbCueNodeKind
    BLOCK: AcbCueNodeKind
    OUTSIDE_LINK: AcbCueNodeKind

class AcbCueEdgeKind(Enum):
    CUE_REFERENCE: AcbCueEdgeKind
    SYNTH_REFERENCE: AcbCueEdgeKind
    TRACK: AcbCueEdgeKind
    ACTION_TRACK: AcbCueEdgeKind
    BLOCK: AcbCueEdgeKind
    EVENT_STREAM: AcbCueEdgeKind
    PARAMETER_STREAM: AcbCueEdgeKind
    COMMAND_TARGET: AcbCueEdgeKind
    OUTSIDE_LINK: AcbCueEdgeKind

class AcbWaveformNameView(Enum):
    PREFERRED: AcbWaveformNameView
    EXACT_WAVEFORM: AcbWaveformNameView
    ASSOCIATED_AWB_ASSET: AcbWaveformNameView

class AcbCueChoiceDomain(Enum):
    SEQUENCE_TRACK: AcbCueChoiceDomain
    SYNTH_REFERENCE: AcbCueChoiceDomain

class AcbCueAwbBank(Enum):
    MEMORY: AcbCueAwbBank
    STREAM: AcbCueAwbBank

class AcbCommandTarget:
    type: AcbCommandTargetType
    index: int

class AcbCueCommand:
    code: int
    dispatcher: AcbCommandDispatcher
    family: AcbCommandFamily
    meaning: AcbCueCommandMeaning
    payload: bytes
    target: AcbCommandTarget | None
    argument_u16: int | None
    argument_u16_2: int | None
    argument_i16: int | None
    time_advance_us: int | None

class AcbScheduledTarget:
    command_index: int
    time_us: int
    target: AcbCommandTarget

class AcbCueCommandStream:
    row_index: int
    table_kind: AcbCommandTableKind
    dispatcher: AcbCommandDispatcher
    raw: bytes
    commands: list[AcbCueCommand]
    scheduled_targets: list[AcbScheduledTarget]
    duration_us: int

class AcbCueName:
    row_index: int
    cue_index: int
    name: str
    name_raw: bytes

class AcbCueReference:
    type: int
    type_name: str
    index: int

class AcbCue:
    row_index: int
    cue_id: int
    reference: AcbCueReference
    name_rows: list[int]
    length: int
    worksize: int
    num_related_waveforms: int
    header_visibility: int

class AcbSynthReference:
    type: int
    type_name: str
    index: int

class AcbSynth:
    row_index: int
    type: int
    command_index: int
    action_track_start_index: int
    num_action_tracks: int
    reference_items_raw: bytes
    reference_items: list[AcbSynthReference]
    track_values_raw: bytes
    track_values: list[int]

class AcbTrack:
    row_index: int
    event_index: int
    command_index: int
    target_type: int
    target_name: str
    target_name_raw: bytes
    target_id: int
    target_acb_name: str
    target_acb_name_raw: bytes
    scope: int
    target_track_no: int

class AcbSequence:
    row_index: int
    type: int
    type_name: str
    playback_ratio: int
    command_index: int
    track_indices: list[int]
    track_values_raw: bytes
    track_values: list[int]
    action_track_start_index: int
    num_action_tracks: int
    watch_action_start_index: int
    num_watch_actions: int
    stop_action_start_index: int
    num_stop_actions: int

class AcbBlock:
    row_index: int
    track_indices: list[int]
    playback_type: int
    num_loops: int
    transition_timing: int
    transition_timing_value: int
    jump_previous_behavior: int
    jump_destination: int
    name_index: int
    length_ms: int
    length_submillisecond: int
    duration_us: int
    start_position_ms: int
    start_position_submillisecond: int
    start_position_us: int
    action_track_start_index: int
    num_action_tracks: int
    destination_blocks: list[int]
    destination_values_raw: bytes

class AcbBlockSequence:
    row_index: int
    type: int
    type_name: str
    playback_ratio: int
    command_index: int
    track_indices: list[int]
    block_indices: list[int]
    track_values_raw: bytes
    track_values: list[int]
    watch_action_start_index: int
    num_watch_actions: int
    stop_action_start_index: int
    num_stop_actions: int

class AcbCueWaveform:
    row_index: int
    id: int
    memory_awb_id: int
    stream_awb_id: int
    stream_awb_port_no: int
    streaming: int
    encode_type: int
    num_channels: int
    loop_flag: int
    sampling_rate: int
    num_samples: int
    extension_data: int

class AcbWaveformExtension:
    row_index: int
    loop_start: int
    loop_end: int

class AcbStringValue:
    row_index: int
    value: str
    value_raw: bytes

class AcbOutsideLink:
    row_index: int
    cue_id: int
    cue_name_string_index: int
    acb_name_string_index: int

class AcbCueNode:
    kind: AcbCueNodeKind
    index: int

class AcbCueEdge:
    from_node: AcbCueNode
    to_node: AcbCueNode
    kind: AcbCueEdgeKind
    ordinal: int

class AcbUnresolvedReference:
    source: AcbCueNode
    type: int
    index: int
    reason: str

class AcbCueAssembly:
    cue_index: int
    nodes: list[AcbCueNode]
    edges: list[AcbCueEdge]
    unresolved: list[AcbUnresolvedReference]
    has_cycle: bool

class AcbWaveformCueView:
    waveform_index: int
    exact_cue_name_rows: list[int]
    preferred_cue_name_rows: list[int]
    associated_awb_cue_name_rows: list[int]

class AcbCueClipPlan:
    waveform_index: int
    start_time_us: int
    awb_wave_id: int | None
    awb_stream_index: int | None
    awb_bank: AcbCueAwbBank | None

class AcbCueBlockPlan:
    block_position: int | None
    block_index: int | None
    name: str
    duration_us: int
    authored_loop_count: int
    render_loop_count: int
    forced_advance: bool
    skipped_empty_hold: bool
    clips: list[AcbCueClipPlan]

class AcbCuePlaybackPlan:
    cue_index: int
    cue_id: int
    cue_name: str
    blocks: list[AcbCueBlockPlan]
    block_count: int
    def wav_bytes(self, acb: Acb, hca_keycode: int = 0, hca_subkey: int | None = None) -> bytes: ...
    def export(self, acb: Acb, output_path: Any, hca_keycode: int = 0, hca_subkey: int | None = None) -> None: ...

class AcbCueChoiceSelection:
    domain: AcbCueChoiceDomain
    node_index: int
    occurrence: int
    option_index: int
    mode: int
    selector_name: str
    selector_value: str
    def __init__(self) -> None: ...

class AcbCuePlanVariant:
    plan: AcbCuePlaybackPlan
    paths: list[list[AcbCueChoiceSelection]]

class AcbCueTerminalPath:
    choices: list[AcbCueChoiceSelection]
    error: str

class AcbCuePlanEnumeration:
    cue_index: int
    variants: list[AcbCuePlanVariant]
    terminal_errors: list[str]
    terminal_paths: list[AcbCueTerminalPath]
    explored_paths: int

class AcbCueSelectorValue:
    name: str
    value: str
    def __init__(self) -> None: ...

class AcbCuePlanSource:
    source_cue_index: int
    source_cue_id: int
    source_cue_name: str
    terminal_cue_index: int
    action_cue_chain: list[int]
    selector_values: list[AcbCueSelectorValue]
    paths: list[list[AcbCueChoiceSelection]]

class AcbResolvedCuePlan:
    plan: AcbCuePlaybackPlan
    sources: list[AcbCuePlanSource]

class AcbCueSheetResolution:
    plans: list[AcbResolvedCuePlan]
    plan_count: int
    non_playable_cues: list[int]
    def filenames(self, include_index_prefix: bool = True) -> list[str]: ...

class AcbCueGraph:
    cues: list[AcbCue]
    cue_names: list[AcbCueName]
    synths: list[AcbSynth]
    sequences: list[AcbSequence]
    tracks: list[AcbTrack]
    action_tracks: list[AcbTrack]
    block_sequences: list[AcbBlockSequence]
    blocks: list[AcbBlock]
    waveforms: list[AcbCueWaveform]
    waveform_extensions: list[AcbWaveformExtension]
    strings: list[AcbStringValue]
    outside_links: list[AcbOutsideLink]
    track_events: list[AcbCueCommandStream]
    legacy_commands: list[AcbCueCommandStream]
    sequence_commands: list[AcbCueCommandStream]
    track_commands: list[AcbCueCommandStream]
    synth_commands: list[AcbCueCommandStream]
    has_embedded_awb: bool
    cue_count: int
    waveform_count: int
    def cue(self, index: int) -> AcbCue: ...
    def cue_index_by_id(self, cue_id: int) -> int | None: ...
    def cue_name(self, index: int) -> str: ...
    def string_value(self, index: int) -> str: ...
    def assemble_cue(self, index: int) -> AcbCueAssembly: ...
    def waveform_cue_views(self) -> list[AcbWaveformCueView]: ...
    def waveform_name(self, index: int, view: AcbWaveformNameView = AcbWaveformNameView.PREFERRED, raw: bool = False) -> str: ...
    def waveform_names(self, view: AcbWaveformNameView = AcbWaveformNameView.PREFERRED, raw: bool = False) -> list[str]: ...

class AcbWaveformInfo:
    index: int
    name: str
    name_raw: bytes
    filename: str
    id: int
    memory_awb_id: int
    stream_awb_id: int
    port_no: int
    streaming: int
    encode_type: int
    loop_flag: bool
    extension_data: int

class WaveformAwbEntry:
    waveform_index: int
    wave_id: int
    awb_index: int
    stream_bank: bool

class AcbInfo:
    source_path: str | None
    name: str
    waveform_count: int
    cue_count: int
    waveforms: list[AcbWaveformInfo]
    table_name: str
    row_count: int
    column_count: int
    has_embedded_awb: bool
    companion_awb_path: str | None
    has_aac_waveforms: bool
    awb_info: AwbInfo | None

class Acb:
    source_path: str | None
    name: str
    waveform_count: int
    cue_count: int
    graph: AcbCueGraph
    has_embedded_awb: bool
    companion_awb_path: str | None
    has_aac_waveforms: bool

    @staticmethod
    def load(source: Any, encoding: str | None = None) -> Acb: ...
    @staticmethod
    def load_bytes(data: bytes, encoding: str | None = None) -> Acb: ...
    def info(self) -> AcbInfo: ...
    def waveform(self, index: int) -> AcbWaveformInfo: ...
    def waveform_awb_entry(self, index: int, awb: Awb | None = None, prefer_stream_bank: bool = False) -> WaveformAwbEntry: ...
    def replace_waveform_bytes(self, index: int, awb: Awb, data: bytes, prefer_stream_bank: bool = False) -> WaveformAwbEntry: ...
    def replace_waveform_file(self, index: int, awb: Awb, input_path: Any, prefer_stream_bank: bool = False) -> WaveformAwbEntry: ...
    def waveform_name(self, index: int) -> str: ...
    def waveform_name_raw(self, index: int) -> bytes: ...
    def waveform_filename(self, index: int, include_index_prefix: bool = True) -> str: ...
    def cue_filename(self, index: int, include_index_prefix: bool = True) -> str: ...
    def selector_options(self, index: int | None = None, max_paths: int = 65_536, max_action_depth: int = 64) -> dict[str, list[str]]: ...
    def resolve_cue(self, index: int, selectors: dict[str, str] | None = None, loop_count: int = 0, advance_after_infinite: bool = True, include_empty_holds: bool = False, block_loop_counts: dict[int, int] | None = None, max_paths: int = 65_536, max_action_depth: int = 64) -> AcbCueSheetResolution: ...
    def resolve_cue_by_id(self, cue_id: int, selectors: dict[str, str] | None = None, loop_count: int = 0, advance_after_infinite: bool = True, include_empty_holds: bool = False, block_loop_counts: dict[int, int] | None = None, max_paths: int = 65_536, max_action_depth: int = 64) -> AcbCueSheetResolution: ...
    def resolve_cues(self, selectors: dict[str, str] | None = None, loop_count: int = 0, advance_after_infinite: bool = True, include_empty_holds: bool = False, block_loop_counts: dict[int, int] | None = None, max_paths: int = 65_536, max_action_depth: int = 64) -> AcbCueSheetResolution: ...
    def cue_plan(self, index: int, loop_count: int = 0, advance_after_infinite: bool = True, include_empty_holds: bool = False, block_loop_counts: dict[int, int] | None = None, selectors: dict[str, str] | None = None, variant: int | None = None, max_paths: int = 65_536, max_action_depth: int = 64) -> AcbCuePlaybackPlan: ...
    def cue_plan_by_id(self, cue_id: int, loop_count: int = 0, advance_after_infinite: bool = True, include_empty_holds: bool = False, block_loop_counts: dict[int, int] | None = None, selectors: dict[str, str] | None = None, variant: int | None = None, max_paths: int = 65_536, max_action_depth: int = 64) -> AcbCuePlaybackPlan: ...
    def cue_wav_bytes(self, index: int, loop_count: int = 0, advance_after_infinite: bool = True, hca_keycode: int = 0, hca_subkey: int | None = None, include_empty_holds: bool = False, block_loop_counts: dict[int, int] | None = None, selectors: dict[str, str] | None = None, variant: int | None = None, max_paths: int = 65_536, max_action_depth: int = 64) -> bytes: ...
    def extract_cue(self, index: int, output_path: Any, loop_count: int = 0, advance_after_infinite: bool = True, hca_keycode: int = 0, hca_subkey: int | None = None, include_empty_holds: bool = False, block_loop_counts: dict[int, int] | None = None, selectors: dict[str, str] | None = None, variant: int | None = None, max_paths: int = 65_536, max_action_depth: int = 64) -> None: ...
    def extract_cues(self, output_dir: Any, loop_count: int = 0, advance_after_infinite: bool = True, hca_keycode: int = 0, hca_subkey: int | None = None, include_empty_holds: bool = False, block_loop_counts: dict[int, int] | None = None, selectors: dict[str, str] | None = None, max_paths: int = 65_536, max_action_depth: int = 64, include_index_prefix: bool = True) -> list[str]: ...
    def waveform_bytes(self, index: int, aac_keycode: int = 0) -> bytes: ...
    def extract_waveform_data(self, index: int, aac_keycode: int = 0) -> bytes: ...
    def extract_waveform_stream_data(self, index: int, aac_keycode: int = 0) -> bytes: ...
    def probe_waveform_aac_encryption(self, index: int, keycode: int) -> AacEncryptionState: ...
    def recover_aac_key(self) -> KeyRecoveryResult: ...
    def embedded_awb_bytes(self) -> bytes | None: ...
    def load_awb(self) -> Awb | None: ...
    def extract_file(self, index: int, output_path: Any, aac_keycode: int = 0) -> None: ...
    def extract(self, output_dir: Any, aac_keycode: int = 0) -> None: ...

def load(source: Any, encoding: str | None = None) -> Acb: ...
def extract(source: Any, output_dir: Any, aac_keycode: int = 0, encoding: str | None = None) -> None: ...

__all__: list[str]
