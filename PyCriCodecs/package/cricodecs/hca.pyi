from typing import Any, ClassVar

from .wav import Wav

class HcaQuality:
    LOWEST: ClassVar["HcaQuality"]
    LOW: ClassVar["HcaQuality"]
    MIDDLE: ClassVar["HcaQuality"]
    HIGH: ClassVar["HcaQuality"]
    HIGHEST: ClassVar["HcaQuality"]

class KeyRecoveryMode:
    INDEPENDENT: ClassVar["KeyRecoveryMode"]
    SHARED_BASE_KEY: ClassVar["KeyRecoveryMode"]

class KeyCandidate:
    key: int
    score: float
    source_count: int
    evidence_count: int
    unknown_high_bits: int
    equivalent_count: int

class KeyRecoveryResult:
    candidates: list[KeyCandidate]
    source_count: int
    evidence_count: int

class HcaCodecChunkType:
    UNKNOWN: ClassVar["HcaCodecChunkType"]
    COMP: ClassVar["HcaCodecChunkType"]
    DEC: ClassVar["HcaCodecChunkType"]

class HcaFileChunk:
    version: int
    header_size: int

class HcaFmtChunk:
    channel_count: int
    sample_rate: int
    frame_count: int
    encoder_delay: int
    encoder_padding: int

class HcaCodecChunk:
    type: HcaCodecChunkType
    frame_size: int
    min_resolution: int
    max_resolution: int
    track_count: int
    channel_config: int
    total_band_count: int
    base_band_count: int
    stereo_band_count: int
    hfr_group_count: int
    bands_per_hfr_group: int
    ms_stereo: bool
    uses_ms_stereo: bool

class HcaVbrChunk:
    enabled: bool
    max_frame_size: int
    noise_level: int

class HcaAthChunk:
    type: int
    uses_curve: bool

class HcaLoopChunk:
    enabled: bool
    start_frame: int
    end_frame: int
    start_delay: int
    end_padding: int

class HcaCipherChunk:
    type: int
    encrypted: bool

class HcaRvaChunk:
    volume: float
    has_volume_scale: bool

class HcaCommentChunk:
    length: int
    has_text: bool

class HcaHeader:
    file: HcaFileChunk
    fmt: HcaFmtChunk
    codec: HcaCodecChunk
    vbr: HcaVbrChunk
    ath: HcaAthChunk
    loop: HcaLoopChunk
    cipher: HcaCipherChunk
    rva: HcaRvaChunk
    comment: HcaCommentChunk

    def sample_count(self) -> int: ...

class HcaEncodeConfig:
    sample_rate: int
    channel_count: int
    version: int
    bitrate: int
    quality: HcaQuality
    loop_enabled: bool
    loop_start: int
    loop_end: int
    ms_stereo: bool
    keycode: int
    subkey: int

class Hca:
    source_path: str | None

    @staticmethod
    def load(source: Any) -> "Hca": ...
    @staticmethod
    def load_bytes(data: bytes) -> "Hca": ...
    def header(self) -> HcaHeader: ...
    def decode(self, keycode: int = 0, subkey: int = 0) -> bytes: ...
    def encrypt(self, cipher_type: int, keycode: int = 0, subkey: int = 0) -> bytes: ...
    def decrypt(self, keycode: int = 0, subkey: int = 0) -> bytes: ...
    def rebuild(self) -> bytes: ...
    def recover_key(self, subkey: int = 0) -> KeyRecoveryResult: ...

NativeHca = Hca

def load(source: Any) -> Hca: ...
def decode(source: Any, keycode: int = 0, subkey: int = 0) -> bytes: ...
def encode(wav: Wav | Any, config: HcaEncodeConfig | None = None) -> bytes: ...
def encrypt(source: Any, cipher_type: int, keycode: int = 0, subkey: int = 0) -> bytes: ...
def decrypt(source: Any, keycode: int = 0, subkey: int = 0) -> bytes: ...
# Returns canonical low-56 candidates. HCA data cannot reveal the original
# 64-bit caller key's upper byte.
def recover_key(
    source: Any | list[Any] | tuple[Any, ...],
    subkeys: int | list[int] | tuple[int, ...] | None = None,
    same_base_key: bool = True,
) -> KeyRecoveryResult: ...

__all__: list[str]
