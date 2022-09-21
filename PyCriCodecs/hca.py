from io import BytesIO, FileIO
from struct import *
from .chunk import *
from typing import BinaryIO
import CriCodecs

from .chunk import HCAType

HcaHeaderStruct = Struct(">4sHH")

class HCA:
    __slots__ = ["stream", "HcaSig", "version", "header_size", "key", "subkey", "hca", "filetype", "wavbytes",
                "riffSignature", "riffSize", "wave", "fmt", "fmtSize", "fmtType", "fmtChannelCount", 
                "fmtSamplingRate", "fmtSamplesPerSec", "fmtSamplingSize", "fmtBitCount", "dataSig", "dataSize", "Flags",
                "AlignmentSamples", "LoopCount", "LoopNum", "LoopType", "LoopStartSample", "LoopStartByte", "LoopEndSample", 
                "LoopEndByte", "looping", "hcabytes"]
    stream: BinaryIO
    HcaSig: bytes
    version: int
    header_size: int
    key: int
    subkey: int
    hca: dict
    filetype: str
    wavbytes: bytearray
    hcabytes: bytearray
    riffSignature: bytes
    riffSize: int
    wave: bytes
    fmt: bytes
    fmtSize: int
    fmtType: int
    fmtChannelCount: int
    fmtSamplingRate: int
    fmtSamplesPerSec: int
    fmtSamplingSize: int
    fmtBitCount: int
    dataSig: bytes
    dataSize: int

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
        if self.HcaSig in [HCAType.HCA.value, HCAType.EHCA.value]:
            self.filetype = "hca"
            if self.HcaSig != HCAType.HCA.value and self.HcaSig != HCAType.EHCA.value:
                raise ValueError("Invalid HCA file.")
            elif self.HcaSig == HCAType.EHCA.value and not self.key:
                self.key = 0xCF222F1FE0748978 # Default HCA key.
            elif self.key < 0:
                raise ValueError("HCA key cannot be a negative.")
            elif self.key > 0xFFFFFFFFFFFFFFFF:
                raise OverflowError("HCA key cannot exceed the maximum size of 8 bytes.")
            elif self.subkey < 0:
                raise ValueError("HCA subkey cannot be a negative.")
            elif self.subkey > 0xFFFF:
                raise OverflowError("HCA subkey cannot exceed 65535.")
        elif self.HcaSig == b"RIFF":
            self.filetype = "wav"
            self.riffSignature, self.riffSize, self.wave, self.fmt, self.fmtSize, self.fmtType, self.fmtChannelCount, self.fmtSamplingRate, self.fmtSamplesPerSec, self.fmtSamplingSize, self.fmtBitCount = WavHeaderStruct.unpack(
                self.stream.read(WavHeaderStruct.size)
            )
            if self.riffSignature == b"RIFF" and self.wave == b'WAVE' and self.fmt == b'fmt ':
                if self.fmtBitCount != 16:
                    raise ValueError(f"WAV bitdepth of {self.fmtBitCount} is not supported, only 16 bit WAV files are supported.")
                elif self.fmtSize != 16:
                    raise ValueError(f"WAV file has an FMT chunk of an unsupported size: {self.fmtSize}, the only supported size is 16.")
                if self.stream.read(4) == b"smpl":
                    self.stream.seek(-4, 1)
                    self.looping = True
                    # Will just be naming the important things here.
                    smplsig, smplesize, _, _, _, _, _, _, _, self.LoopCount, _, _, _, self.LoopStartSample, self.LoopEndSample, _, _ = WavSmplHeaderStruct.unpack(
                        self.stream.read(WavSmplHeaderStruct.size)
                    )
                    if self.LoopCount != 1:
                        self.looping = False # Unsupported multiple looping points, so backtracks, and ignores looping data.
                        self.stream.seek(-WavSmplHeaderStruct.size, 1)
                        self.stream.seek(8 + smplesize, 1)
                else:
                    self.stream.seek(-4, 1)
                    self.looping = False
                if self.stream.read(4) == b"note": # There's no use for this on ADX.
                    len = self.stream.read(4)
                    self.stream.seek(len+4) # + 1? + padding maybe?
                else:
                    self.stream.seek(-4, 1)
                if self.stream.read(4) == b"data":
                    self.stream.seek(-4, 1)
                    self.dataSig, self.dataSize = WavDataHeaderStruct.unpack(
                        self.stream.read(WavDataHeaderStruct.size)
                    )
                else:
                    raise ValueError("Invalid or an unsupported wav file.")
                self.stream.seek(0)
        else:
            raise ValueError("Invalid HCA or WAV file.")
        
        # TODO Redo header parsing here as well, instead of it being only on C++.
        self.hca = dict(Header=self.HcaSig, version=hex(self.version), HeaderSize=self.header_size)
    
    def info(self) -> dict:
        if self.filetype == "hca":
            return self.hca
        elif self.filetype == "wav":
            wav = dict(RiffSignature=self.riffSignature.decode(), riffSize=self.riffSize, WaveSignature=self.wave.decode(), fmtSignature=self.fmt.decode(), fmtSize=self.fmtSize, fmtType=self.fmtType, fmtChannelCount=self.fmtChannelCount, fmtSamplingRate=self.fmtSamplingRate, fmtSamplesPerSec=self.fmtSamplesPerSec, fmtSamplingSize=self.fmtSamplingSize, fmtBitCount=self.fmtBitCount, dataSignature=self.dataSig.decode(), dataSize=self.dataSize)
            return wav
    
    def decode(self) -> bytearray:
        if self.filetype == "wav":
            raise ValueError("Input type for decoding must be an HCA file.")
        self.wavbytes = CriCodecs.HcaDecode(self.stream.read(), self.header_size, self.key, self.subkey)
        return self.wavbytes
    
    def encode(self, force_not_looping: bool = False) -> bytearray:
        if self.filetype == "hca":
            raise ValueError("Input type for encoding must be a WAV file.")
        if force_not_looping == False:
            force_not_looping = 0
        elif force_not_looping == True:
            force_not_looping = 1
        else:
            raise ValueError("Forcing the encoder to not loop is by either False or True.")
        self.hcabytes = CriCodecs.HcaEncode(self.stream.read(), force_not_looping)
        return self.hcabytes