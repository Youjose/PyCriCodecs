from typing import Any, ClassVar

class CpkMode:
    MODE0: ClassVar["CpkMode"]
    MODE1: ClassVar["CpkMode"]
    MODE2: ClassVar["CpkMode"]
    MODE3: ClassVar["CpkMode"]

class CpkPreset:
    CUSTOM: ClassVar["CpkPreset"]
    ID: ClassVar["CpkPreset"]
    ID_GROUP: ClassVar["CpkPreset"]
    FILENAME: ClassVar["CpkPreset"]
    FILENAME_ID: ClassVar["CpkPreset"]
    FILENAME_GROUP: ClassVar["CpkPreset"]
    FILENAME_ID_GROUP: ClassVar["CpkPreset"]

class CpkEntry:
    id: int
    toc_index: int
    dirname: str
    dirname_raw: bytes
    filename: str
    filename_raw: bytes
    full_path: str
    file_offset: int
    file_size: int
    extract_size: int
    is_compressed: bool
    request_compress: bool
    user_string: str

class CpkInfo:
    source_path: str | None
    file_count: int
    alignment: int
    content_offset: int
    has_toc: bool
    has_etoc: bool
    has_itoc: bool
    has_gtoc: bool
    tver: str
    comment: str
    layout_mode: CpkMode
    preset: CpkPreset
    has_declared_preset: bool
    declared_preset: CpkPreset
    etoc_local_dir: str
    files: list[CpkEntry]

class Cpk:
    source_path: str | None
    file_count: int
    alignment: int
    content_offset: int
    has_toc: bool
    has_etoc: bool
    has_itoc: bool
    has_gtoc: bool
    tver: str
    comment: str
    layout_mode: CpkMode
    preset: CpkPreset
    has_declared_preset: bool
    declared_preset: CpkPreset
    declared_preset_for_save: CpkPreset
    etoc_local_dir: str
    enable_toc: bool
    enable_etoc: bool
    enable_itoc: bool
    enable_gtoc: bool
    files: list[CpkEntry]

    @staticmethod
    def create(preset: CpkPreset = ..., align: int = ..., encoding: str | None = None) -> "Cpk": ...
    @staticmethod
    def load(source: Any, encoding: str | None = None) -> "Cpk": ...
    @staticmethod
    def load_bytes(data: bytes, encoding: str | None = None) -> "Cpk": ...
    def info(self) -> CpkInfo: ...
    def file_info(self, index: int) -> CpkEntry: ...
    def file_infos(self) -> list[CpkEntry]: ...
    def file_bytes(self, index: int) -> bytes: ...
    def add_file(self, local_path: Any, cpk_path: str, compress: bool = False, id: int | None = None) -> None: ...
    def add_bytes(self, data: bytes, cpk_path: str, compress: bool = False, id: int | None = None) -> None: ...
    def replace_file(self, index: int, local_path: Any, compress: bool | None = None) -> None: ...
    def replace_bytes(self, index: int, data: bytes, compress: bool | None = None) -> None: ...
    def remove(self, index: int) -> None: ...
    def rename(self, index: int, archive_path: str) -> None: ...
    def set_file_path(self, index: int, archive_path: str) -> None: ...
    def set_dirname(self, index: int, dirname: str) -> None: ...
    def set_filename(self, index: int, filename: str) -> None: ...
    def set_request_compress(self, index: int, compress: bool) -> None: ...
    def set_all_request_compress(self, compress: bool) -> None: ...
    def move_file(self, from_index: int, to_index: int) -> None: ...
    def extract(self, output_dir: Any, disambiguate_conflicts: bool = True) -> None: ...
    def encrypt(self) -> bytes: ...
    def decrypt(self) -> bytes: ...
    def save(self, output_path: Any | None = None) -> bytes | None: ...
    def save_bytes(self) -> bytes: ...

def create(preset: CpkPreset = ..., align: int = ..., encoding: str | None = None) -> Cpk: ...
def load(source: Any, *, encoding: str | None = None) -> Cpk: ...
def extract(source: Any, output_dir: Any, disambiguate_conflicts: bool = True, *, encoding: str | None = None) -> None: ...

__all__: list[str]
