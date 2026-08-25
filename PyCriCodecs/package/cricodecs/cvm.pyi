from typing import Any, ClassVar, Sequence, overload

class CvmBuildScriptFile:
    index: int
    archive_path: str
    source_path: str

class CvmHeader:
    total_size: int
    chunk_length: int
    sector_table_entry_count: int
    sector_table: list[int]
    iso_start_sector: int
    zone_sector_index: int
    flags: int
    maker_id: str
    filesystem_id: str
    recording_date: str

class CvmZoneLayout:
    sector_length_1: int
    sector_length_2: int
    zone_sector: int
    iso_sector: int
    iso_length: int
    data_sector: int
    data_length: int
    chunk_length: int

class CvmPrimaryVolume:
    system_identifier: str
    volume_identifier: str
    volume_space_size: int
    logical_block_size: int
    volume_set_identifier: str
    publisher_identifier: str
    data_preparer_identifier: str
    application_identifier: str

class CvmEntry:
    index: int
    path: str
    extent_sector: int
    size: int

class CvmDirectoryEntry:
    name: str
    archive_path: str
    size: int
    is_directory: bool

class CvmDirectoryRecord:
    directory_path: str
    extent_sector: int
    byte_size: int
    entries: list[CvmDirectoryEntry]

class CvmRofsFileInfo:
    name: str
    size: int
    is_directory: bool

class CvmRofsVolumeInfo:
    name: str
    source_path: str
    current_directory: str
    is_default: bool
    is_scrambled: bool

class CvmRofsScrambleInfo:
    volume_name: str
    is_scrambled: bool
    initial_sector: int
    current_sector: int
    volume_token: int
    raw_words: tuple[int, ...]

class CvmRofsTransferStatus:
    IDLE: ClassVar["CvmRofsTransferStatus"]
    TRANSFERRING: ClassVar["CvmRofsTransferStatus"]
    COMPLETE: ClassVar["CvmRofsTransferStatus"]
    ERROR: ClassVar["CvmRofsTransferStatus"]

class CvmRofsSeekMode:
    SET: ClassVar["CvmRofsSeekMode"]
    CURRENT: ClassVar["CvmRofsSeekMode"]
    END: ClassVar["CvmRofsSeekMode"]

class CvmRofsRangeHandle:
    volume_name: str
    start_sector: int
    sector_count: int
    byte_size: int
    current_sector: int
    last_transfer_sector_count: int
    last_transfer_status: CvmRofsTransferStatus

class CvmBuildFile:
    archive_path: str
    source_path: str

    @overload
    def __init__(self) -> None: ...
    @overload
    def __init__(self, archive_path: Any, source_path: Any) -> None: ...

class CvmBuildInput:
    disc_name: str
    media: str
    system_identifier: str
    volume_identifier: str
    volume_set_identifier: str
    publisher_identifier: str
    data_preparer_identifier: str
    application_identifier: str
    recording_date: str
    files: list[CvmBuildFile]

    def __init__(
        self,
        disc_name: str = "",
        files: Sequence[CvmBuildFile] = (),
        recording_date: str = "",
        media: str = "DVD",
        system_identifier: str = "CRI ROFS",
        volume_identifier: str = "",
        volume_set_identifier: str = "",
        publisher_identifier: str = "",
        data_preparer_identifier: str = "",
        application_identifier: str = "",
    ) -> None: ...

    @staticmethod
    def from_script(script: "CvmBuildScript") -> "CvmBuildInput": ...
    @staticmethod
    def from_directory(
        input_dir: str,
        disc_name: str = "",
        recording_date: str = "",
        media: str = "DVD",
        system_identifier: str = "CRI ROFS",
        volume_identifier: str = "",
        volume_set_identifier: str = "",
        publisher_identifier: str = "",
        data_preparer_identifier: str = "",
        application_identifier: str = "",
    ) -> "CvmBuildInput": ...

CvmBuildConfig = CvmBuildInput

class CvmInfo:
    source_path: str | None
    entry_count: int
    disc_name: str
    is_scrambled: bool
    has_accessible_contents: bool
    entries: list[CvmEntry]
    header: CvmHeader
    zone: CvmZoneLayout
    primary_volume: CvmPrimaryVolume

class CvmBuildScriptInfo:
    source_path: str | None
    disc_name: str
    media: str
    file_count: int

class CvmBuildScript:
    source_path: str | None
    disc_name: str
    media: str
    system_identifier: str
    volume_identifier: str
    volume_set_identifier: str
    publisher_identifier: str
    data_preparer_identifier: str
    application_identifier: str
    recording_date: str
    files: list[CvmBuildScriptFile]

    @staticmethod
    def load(path: str) -> "CvmBuildScript": ...
    @staticmethod
    def parse(script_text: str, script_directory: str = "") -> "CvmBuildScript": ...
    def info(self) -> CvmBuildScriptInfo: ...
    def to_text(self) -> str: ...

class Cvm:
    source_path: str | None
    entry_count: int
    entries: list[CvmEntry]
    header: CvmHeader
    zone: CvmZoneLayout
    primary_volume: CvmPrimaryVolume
    embedded_iso_offset: int
    embedded_iso_size: int
    embedded_iso_sector_count: int
    is_scrambled: bool
    has_accessible_contents: bool
    disc_name: str
    system_identifier: str
    volume_identifier: str
    volume_set_identifier: str
    publisher_identifier: str
    data_preparer_identifier: str
    application_identifier: str
    recording_date: str

    @staticmethod
    def load(source: Any, key: str = "") -> "Cvm": ...
    @staticmethod
    def load_bytes(data: bytes, key: str = "") -> "Cvm": ...
    def info(self) -> CvmInfo: ...
    def entry(self, index: int) -> CvmEntry: ...
    def find_entry(self, path: str) -> CvmEntry | None: ...
    def directory_record(self, path: str = "") -> CvmDirectoryRecord: ...
    def file_bytes(self, index: int) -> bytes: ...
    def file_bytes_at(self, archive_path: str) -> bytes: ...
    def extract_file(self, index: int, output_path: Any) -> None: ...
    def extract(self, output_dir: Any) -> None: ...
    def add_file(self, source_path: Any, archive_path: str) -> int: ...
    def add_bytes(self, data: bytes, archive_path: str) -> int: ...
    def replace_file(self, index: int, source_path: Any) -> None: ...
    def replace_file_at(self, archive_path: str, source_path: Any) -> None: ...
    @overload
    def replace_bytes(self, index: int, data: bytes) -> None: ...
    @overload
    def replace_bytes(self, archive_path: str, data: bytes) -> None: ...
    def replace_bytes_at(self, archive_path: str, data: bytes) -> None: ...
    @overload
    def remove(self, index: int) -> None: ...
    @overload
    def remove(self, archive_path: str) -> None: ...
    def remove_at(self, archive_path: str) -> None: ...
    def move_file(self, from_index: int, to_index: int) -> None: ...
    def rename(self, index: int, archive_path: str) -> None: ...
    def rename_at(self, existing_archive_path: str, archive_path: str) -> None: ...
    @overload
    def save(self, key: str = "") -> bytes: ...
    @overload
    def save(self, output_path: Any, key: str = "") -> None: ...
    def save_bytes(self, key: str = "") -> bytes: ...
    def script_text(self) -> str: ...
    def export_script(self, output_path: Any) -> None: ...

class CvmVolumeSet:
    volume_count: int
    default_volume_name: str | None

    def __init__(self) -> None: ...

    @staticmethod
    def rofs_sector_length() -> int: ...

    def mount(self, volume_name: str, source: Any) -> None: ...
    def mount_path(self, volume_name: str, path: str) -> None: ...
    def mount_bytes(self, volume_name: str, data: bytes) -> None: ...
    def unmount(self, volume_name: str) -> None: ...
    def set_default_volume(self, volume_name: str) -> None: ...
    def default_volume(self) -> str: ...
    def volume_info(self, volume_name: str) -> CvmRofsVolumeInfo: ...
    def default_volume_info(self) -> CvmRofsVolumeInfo: ...
    def switch_image_path(self, volume_name: str, path: str) -> None: ...
    def switch_image_bytes(self, volume_name: str, data: bytes) -> None: ...
    def current_directory(self, volume_name: str) -> str | None: ...
    def find_entry(self, runtime_path: str) -> CvmEntry | None: ...
    def directory_record(self, runtime_path: str = "") -> CvmDirectoryRecord: ...
    def directory_record_for_volume(self, volume_name: str) -> CvmDirectoryRecord: ...
    def file_exists(self, runtime_path: str) -> bool: ...
    def file_exists_relative(
        self, relative_path: str, rofs_directory_record: bytes
    ) -> bool: ...
    def file_size(self, runtime_path: str) -> int: ...
    def file_size64(self, runtime_path: str) -> int: ...
    def file_size_relative(
        self, relative_path: str, rofs_directory_record: bytes
    ) -> int: ...
    def file_size64_relative(
        self, relative_path: str, rofs_directory_record: bytes
    ) -> int: ...
    def file_bytes(self, runtime_path: str) -> bytes: ...
    def file_bytes_relative(
        self, relative_path: str, rofs_directory_record: bytes
    ) -> bytes: ...
    def open_file(self, runtime_path: str) -> CvmRofsRangeHandle: ...
    def open_file_relative(
        self, relative_path: str, rofs_directory_record: bytes
    ) -> CvmRofsRangeHandle: ...
    def open_range(
        self, volume_name: str, start_sector: int, sector_count: int
    ) -> CvmRofsRangeHandle: ...
    def read_sectors(self, handle: CvmRofsRangeHandle, sector_count: int) -> bytes: ...
    def seek(
        self,
        handle: CvmRofsRangeHandle,
        sector_offset: int,
        seek_mode: CvmRofsSeekMode,
    ) -> int: ...
    def tell(self, handle: CvmRofsRangeHandle) -> int: ...
    def status(self, handle: CvmRofsRangeHandle) -> CvmRofsTransferStatus: ...
    def close(self, handle: CvmRofsRangeHandle) -> None: ...
    def stop_transfer(self, handle: CvmRofsRangeHandle) -> None: ...
    def transferred_bytes(self, handle: CvmRofsRangeHandle) -> int: ...
    def transferred_bytes64(self, handle: CvmRofsRangeHandle) -> int: ...
    def change_directory(self, runtime_path: str) -> None: ...
    def set_current_directory(self, source: Any) -> None: ...
    def set_current_directory_path(self, runtime_path: str) -> None: ...
    def set_current_directory_record(self, rofs_directory_record: bytes) -> None: ...
    def set_current_directory_iso(
        self, volume_name: str, iso_directory_record: bytes
    ) -> None: ...
    def set_current_directory_iso_count(
        self,
        volume_name: str,
        iso_directory_record: bytes,
        iso_directory_sector_count: int,
    ) -> None: ...
    def set_current_directory_iso_scramble(
        self,
        volume_name: str,
        iso_directory_record: bytes,
        scramble_info: CvmRofsScrambleInfo,
    ) -> None: ...
    def set_current_directory_iso_count_scramble(
        self,
        volume_name: str,
        iso_directory_record: bytes,
        iso_directory_sector_count: int,
        scramble_info: CvmRofsScrambleInfo,
    ) -> None: ...
    def load_iso_directory_record(self, runtime_path: str) -> bytes: ...
    def load_rofs_directory_record(self, runtime_path: str, max_entries: int) -> bytes: ...
    def rofs_num_files(self, runtime_path: str = "") -> int: ...
    def rofs_num_files_for_volume(self, volume_name: str) -> int: ...
    @staticmethod
    def rofs_num_files_record(rofs_directory_record: bytes) -> int: ...
    @staticmethod
    def rofs_num_files_from_record(rofs_directory_record: bytes) -> int: ...
    def rofs_directory_info(self, runtime_path: str = "") -> list[CvmRofsFileInfo]: ...
    def rofs_directory_info_for_volume(self, volume_name: str) -> list[CvmRofsFileInfo]: ...
    @staticmethod
    def rofs_directory_info_record(
        rofs_directory_record: bytes,
    ) -> list[CvmRofsFileInfo]: ...
    @staticmethod
    def rofs_directory_info_from_record(
        rofs_directory_record: bytes,
    ) -> list[CvmRofsFileInfo]: ...
    def scramble_info(self, runtime_path: str) -> CvmRofsScrambleInfo: ...
    @staticmethod
    def advance_scramble_info(
        scramble_info: CvmRofsScrambleInfo, sector_count: int
    ) -> None: ...
    def descramble(
        self, sector_data: bytes, scramble_info: CvmRofsScrambleInfo
    ) -> bytes: ...

def load_script(path: Any) -> CvmBuildScript: ...
def parse_script(
    script_text: str, script_directory: Any | None = None
) -> CvmBuildScript: ...
def load(source: Any, key: str = "") -> Cvm: ...
def extract(source: Any, output_dir: str, key: str = "") -> None: ...
@overload
def build(config: CvmBuildInput, key: str = "") -> bytes: ...
@overload
def build(config: CvmBuildScript, key: str = "") -> bytes: ...
@overload
def build(
    config: CvmBuildInput, output_path: Any, key: str = ""
) -> None: ...
@overload
def build(
    config: CvmBuildScript, output_path: Any, key: str = ""
) -> None: ...
@overload
def build(
    input_dir: Any,
    disc_name: str = "",
    recording_date: str = "",
    media: str = "DVD",
    system_identifier: str = "CRI ROFS",
    volume_identifier: str = "",
    volume_set_identifier: str = "",
    publisher_identifier: str = "",
    data_preparer_identifier: str = "",
    application_identifier: str = "",
    key: str = "",
) -> bytes: ...
def build_from_input(input: CvmBuildInput, key: str = "") -> bytes: ...
def build_from_script(script: CvmBuildScript, key: str = "") -> bytes: ...
def build_to_file_from_input(
    output_path: str, input: CvmBuildInput, key: str = ""
) -> None: ...
def build_to_file_from_script(
    output_path: str, script: CvmBuildScript, key: str = ""
) -> None: ...
def export_script(
    input_dir: Any,
    disc_name: str = "",
    recording_date: str = "",
    media: str = "DVD",
    system_identifier: str = "CRI ROFS",
    volume_identifier: str = "",
    volume_set_identifier: str = "",
    publisher_identifier: str = "",
    data_preparer_identifier: str = "",
    application_identifier: str = "",
) -> str: ...

__all__: list[str]
