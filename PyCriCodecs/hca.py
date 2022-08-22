from io import BytesIO, FileIO
from struct import *
from chunk import *
from typing import BinaryIO
import CriCodecs

from .chunk import HCAType

HcaHeaderStruct = Struct(">4sHH")

class HCA:
    __slots__ = ["stream", "HcaSig", "version", "header_size", "key", "subkey", "hca", "filetype", "wavbytes"]
    stream: BinaryIO
    HcaSig: bytes
    version: int
    header_size: int
    key: int
    subkey: int
    hca: dict
    filetype: str
    wavbytes: bytearray

    def __init__(self, stream: BinaryIO, key: int = 0, subkey: int = 0) -> None:
        if type(stream) == str:
            self.stream = FileIO(stream)
        else:
            self.stream = BytesIO(stream)
        if type(key) == str:
            self.key = int(key, 16)
        else:
            self.key = key
        if type(subkey) == str:
            self.subkey = int(subkey, 16)
        else:
            self.subkey = subkey
        self.Pyparse_header()
    

    def Pyparse_header(self) -> None:
        self.HcaSig, self.version, self.header_size = HcaHeaderStruct.unpack(
            self.stream.read(HcaHeaderStruct.size)
        )
        self.stream.seek(0)
        self.filetype = "hca"
        if self.HcaSig != HCAType.HCA.value and self.HcaSig != HCAType.EHCA.value:
            raise ValueError("Invalid HCA file.")
        elif self.HcaSig == HCAType.EHCA and not self.key:
            self.key = 0xCF222F1FE0748978 # Default HCA key.
        elif self.key < 0:
            raise ValueError("HCA key cannot be a negative.")
        elif self.key > 0xFFFFFFFFFFFFFFFF:
            raise OverflowError("HCA key cannot exceed the maximum size of 8 bytes.")
        elif self.subkey < 0:
            raise ValueError("HCA subkey cannot be a negative.")
        elif self.subkey > 0xFFFF:
            raise OverflowError("HCA subkey cannot exceed 65535.")
        
        # TODO Redo header parsing here as well instead of it being only on C++.
        self.hca = dict(Header=self.HcaSig, version=hex(self.version), HeaderSize=self.header_size)
    
    def info(self) -> dict:
        if self.filetype == "hca":
            return self.hca
    
    def decode(self) -> bytearray:
        self.wavbytes = CriCodecs.HcaDecode(self.stream.read(), self.header_size, self.key, self.subkey)
        return self.wavbytes