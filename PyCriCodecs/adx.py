from io import BytesIO, FileIO
from struct import *
from .chunk import *
from typing import BinaryIO
import CriCodecs

AdxHeaderStruct = Struct(">HHBBBBIIHBB")
AdxLoopHeaderStruct = Struct(">HHHHIIII")

class ADX:
    __slots__ = ["filename", "filetype", "wavStream", "sfaStream", "Adxsignature", "dataOffset", "Encoding",
                "Blocksize", "SampleBitdepth", "channelCount", "SamplingRate", "SampleCount", "HighpassFrequency",
                "AdxVersion", "riffSignature", "riffSize", "wave", "fmt", "fmtSize", "fmtType", "fmtChannelCount", 
                "fmtSamplingRate", "fmtSamplesPerSec", "fmtSamplingSize", "fmtBitCount", "dataSig", "dataSize", "Flags",
                "AlignmentSamples", "LoopCount", "LoopNum", "LoopType", "LoopStartSample", "LoopStartByte", "LoopEndSample", 
                "LoopEndByte", "looping"]
    filename: str
    filetype: str
    wavStream: FileIO
    sfaStream: FileIO
    Adxsignature: int
    dataOffset: int
    Encoding: int
    Blocksize: int
    SampleBitdepth: int
    channelCount: int
    sampleCount: int
    HighpassFrequency: int
    AdxVersion: int
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
    Flags: int
    AlignmentSamples: int
    LoopCount: int
    LoopNum: int
    LoopType: int
    LoopStartSample: int
    LoopStartByte: int
    LoopEndSample: int
    LoopEndByte: int
    looping: bool
    

    def __init__(self, filename) -> None:
        if type(filename) == str:
            stream = FileIO(filename)
        else:
            stream = BytesIO(filename)
        self.load_info(stream)

    def load_info(self, stream: BinaryIO) -> None:
        magic = stream.read(4)
        stream.seek(0)
        if magic[:2] == b"\x80\x00":
            self.filetype = "adx"
            self.sfaStream = stream
        elif magic == b"RIFF":
            self.filetype = "wav"
            self.wavStream = stream
        if self.filetype == "wav":
            self.riffSignature, self.riffSize, self.wave, self.fmt, self.fmtSize, self.fmtType, self.fmtChannelCount, self.fmtSamplingRate, self.fmtSamplesPerSec, self.fmtSamplingSize, self.fmtBitCount = WavHeaderStruct.unpack(
            self.wavStream.read(WavHeaderStruct.size)
            )
            if self.riffSignature == b"RIFF" and self.wave == b'WAVE' and self.fmt == b'fmt ':
                if self.wavStream.read(4) == b"smpl":
                    self.wavStream.seek(-4, 1)
                    self.looping = True
                    # Will just be naming the important things here.
                    smplsig, smplesize, r1, r2, r3, r4, r5, r6, r7, self.LoopCount, r8, r9, r10, self.LoopStartSample, self.LoopEndSample, r11, r12 = WavSmplHeaderStruct.unpack(
                        self.wavStream.read(WavSmplHeaderStruct.size)
                    )
                    if self.LoopCount != 1:
                        self.looping = False # Unsupported multiple looping points, so backtracks, and ignores looping data.
                        self.wavStream.seek(-WavSmplHeaderStruct.size, 1)
                        self.wavStream.seek(8 + smplesize, 1)
                else:
                    self.wavStream.seek(-4, 1)
                    self.looping = False
                if self.wavStream.read(4) == b"note": # There's no use for this on ADX.
                    len = self.wavStream.read(4)
                    self.wavStream.seek(len+4) # + 1? + padding maybe?
                else:
                    self.wavStream.seek(-4, 1)
                if self.wavStream.read(4) == b"data":
                    self.wavStream.seek(-4, 1)
                    self.dataSig, self.dataSize = WavDataHeaderStruct.unpack(
                        self.wavStream.read(WavDataHeaderStruct.size)
                    )
                else:
                    raise ValueError("Invalid or an unsupported wav file.")
                self.wavStream.seek(0)
                return
            else:
                raise ValueError("Invalid wav file.")
        elif self.filetype == "adx":
            self.looping = False
            self.Adxsignature, self.dataOffset, self.Encoding, self.Blocksize, self.SampleBitdepth, self.channelCount, self.SamplingRate, self.SampleCount, self.HighpassFrequency, self.AdxVersion, self.Flags = AdxHeaderStruct.unpack(
            self.sfaStream.read(AdxHeaderStruct.size)
            )
            if self.Adxsignature == 0x8000:
                if self.AdxVersion == 4 or self.AdxVersion == 3:
                    if self.AdxVersion == 4:
                        self.sfaStream.seek(4 + 4 * self.channelCount, 1)
                    if self.sfaStream.tell() + 24 <= self.dataOffset:
                        self.AlignmentSamples, self.LoopCount, self.LoopNum, self.LoopType, self.LoopStartSample, self.LoopStartByte, self.LoopEndSample, self.LoopEndByte = AdxLoopHeaderStruct.unpack(
                            self.sfaStream.read(AdxLoopHeaderStruct.size)
                        )
                        self.looping = True
                self.sfaStream.seek(0)
                return
            else:
                raise ValueError("Invalid adx file.")



    # Decodes ADX to WAV.
    def decode(self) -> bytearray:

        if self.filetype != "adx":
            raise ValueError("Not an ADX file")
        elif self.Adxsignature != 0x8000:
            raise ValueError("Not an ADX file")
        elif self.Encoding == 0x11 or self.Encoding == 0x10 or self.AdxVersion == 6 or self.Blocksize == 0 or self.SampleBitdepth == 0:
            raise NotImplementedError("Unsupported AHX file.")
        elif not (self.Encoding == 3 or self.Encoding == 4 or self.Encoding == 2):
            raise NotImplementedError("Unsupported ADX Encoding.")
        elif self.SampleBitdepth != 4:
            raise NotImplementedError("Unsupported Bitdepth.")
        elif self.Flags == 0x8 or self.Flags == 0x9:
            raise NotImplementedError("Encrypted ADX/AHX detected, unsupported yet.")
        if self.LoopCount != 1:
            self.looping = False
        
        fmtChannelCount = self.channelCount
        fmtSamplingRate = self.SamplingRate
        fmtSamplingSize = fmtChannelCount * 2
        fmtSamplesPerSec = fmtSamplingRate * fmtSamplingSize
        dataSize = self.SampleCount * fmtSamplingSize
        if self.looping:
            riffSize = dataSize + 0x68
        else:
            riffSize = dataSize + 0x24

        WavHeader = WavHeaderStruct.pack(
                                        b"RIFF",
                                        riffSize,
                                        b"WAVE",
                                        b"fmt ",
                                        0x10,
                                        1,
                                        fmtChannelCount,
                                        fmtSamplingRate,
                                        fmtSamplesPerSec,
                                        fmtSamplingSize,
                                        0x10,
                                        )
        
        if self.looping:
            WavSmpl = WavSmplHeaderStruct.pack(
                                              b"smpl",
                                              0x3C,
                                              0,
                                              0,
                                              0,
                                              0,
                                              0,
                                              0,
                                              0,
                                              1,
                                              0,
                                              0,
                                              0,
                                              self.LoopStartSample,
                                              self.LoopEndSample,
                                              0,
                                              0,
                                              )

        WavData = WavDataHeaderStruct.pack(
                                          b"data",
                                          dataSize
                                          )
        
        outfile = bytearray()
        outfile.extend(WavHeader)
        if self.looping:
            outfile.extend(WavSmpl)
        outfile.extend(WavData)
        outfile.extend(CriCodecs.AdxDecode(self.sfaStream.read()))
        self.sfaStream.close()
        return outfile
            
    # Encodes WAV to ADX.
    def encode(self, Blocksize = 0x12, AdxVersion = 0x4, DataOffset = 0x011C) -> bytearray:

        if self.filetype != "wav":
            raise ValueError("Not a WAV file or an unsupported file version.")
        elif self.riffSignature != b"RIFF" and self.wave != b'WAVE' and self.fmt != b'fmt ' and self.dataSig != b'data':
            raise ValueError("Not a WAV file or an unsupported file version.")
        elif not (AdxVersion == 3 or AdxVersion == 4 or AdxVersion == 5):
            raise NotImplementedError("Unsupported ADX version, Supported versions are: 3, 4 and 5.")
        # elif AdxVersion == 0x10 or AdxVersion == 0x11:
        #     raise NotImplementedError("Unsupported AHX encoding.")
        if AdxVersion == 5: # 5 is supposedly like 4 but without looping support.
            self.looping = False
        
        if self.looping:
            if AdxVersion != 3:
                assert DataOffset > (0x38 - 0xC)
            else:
                assert DataOffset > 0x38
        else:
            if AdxVersion == 4 or AdxVersion == 5:
                assert DataOffset > 0x20
        
        channelCount = self.fmtChannelCount
        SamplingRate = self.fmtSamplingRate
        sampleCount = self.dataSize // self.fmtSamplingSize
        Encoding = 0x3 # Only supported encoding version.
        AdxHeader = AdxHeaderStruct.pack(
                                        0x8000, # Signature.
                                        DataOffset, # DataOffset, seems static.
                                        Encoding, # Encoding.
                                        Blocksize, # Blocksize, set at 0x12.
                                        0x4, # Sample Bitdepth.
                                        channelCount,
                                        SamplingRate,
                                        sampleCount,
                                        0x1F4, # Highpass Frequency, always 0x1F4.(?)
                                        AdxVersion, # Version, 4 and 3 seems the same? Version 5 same as 4, does not support looping.
                                        0x0 # Flags.
                                        )
        AdxFooter = pack(">HH",
                        0x8001, # EOF scale.
                        Blocksize-4 # 0xE, Padding size, can be anything. My conclusion here is that it's (blocksize - 4)
                        ) + bytearray(Blocksize-4)
        
        if self.looping:
            AlignmentSamples = (self.LoopStartSample if self.LoopStartSample % ((Blocksize - 2)*2) == 0 else (self.LoopStartSample + ((Blocksize - 2)*2) - self.LoopStartSample % ((Blocksize - 2)*2))) - self.LoopStartSample
            self.LoopStartSample += AlignmentSamples
            self.LoopEndSample += AlignmentSamples
            self.LoopStartByte = ((self.LoopStartSample - (self.LoopStartSample % ((Blocksize-2) * self.fmtChannelCount))) / ((Blocksize-2) * self.fmtChannelCount)) * Blocksize * self.fmtChannelCount + DataOffset + 4 + (self.LoopStartSample % ((Blocksize-2) * self.fmtChannelCount)) * self.fmtChannelCount
            self.LoopEndByte = ((self.LoopEndSample - (self.LoopEndSample % ((Blocksize-2) * self.fmtChannelCount))) / ((Blocksize-2) * self.fmtChannelCount)) * Blocksize * self.fmtChannelCount + DataOffset + 4 + (self.LoopEndSample % ((Blocksize-2) * self.fmtChannelCount)) * self.fmtChannelCount
            AdxLoop = AdxLoopHeaderStruct.pack(
                                              AlignmentSamples,
                                              1,
                                              0,
                                              1,
                                              self.LoopStartSample,
                                              int(self.LoopStartByte),
                                              self.LoopEndSample,
                                              int(self.LoopEndByte)
                                              )

        outfile = bytearray()
        outfile.extend(AdxHeader)
        if AdxVersion == 4 or AdxVersion == 5:
            outfile += bytearray(4 + 4 * self.fmtChannelCount)
        # It's good to standardize the header data size before the audio data begins.
        # This way we can split it into SFA chunks. In this case, 0x120 is the most common size I saw.
        if self.looping:
            outfile.extend(AdxLoop)
            if AdxVersion == 3:
                outfile += bytearray(DataOffset-22-0x18)
            else:
                outfile += bytearray(DataOffset-22-0x24)
        else:
            outfile += bytearray(DataOffset-22)
        outfile.extend(b'(c)CRI')
        outfile.extend(CriCodecs.AdxEncode(self.wavStream.read(), Blocksize))
        self.wavStream.close()
        outfile.extend(AdxFooter)
        return outfile

    
    def info(self) -> dict:
        if self.filetype == "wav":
            wav = dict(RiffSignature=self.riffSignature.decode(), riffSize=self.riffSize, WaveSignature=self.wave.decode(), fmtSignature=self.fmt.decode(), fmtSize=self.fmtSize, fmtType=self.fmtType, fmtChannelCount=self.fmtChannelCount, fmtSamplingRate=self.fmtSamplingRate, fmtSamplesPerSec=self.fmtSamplesPerSec, fmtSamplingSize=self.fmtSamplingSize, fmtBitCount=self.fmtBitCount, dataSignature=self.dataSig.decode(), dataSize=self.dataSize)
            return wav
        elif self.filetype == "adx":
            adx = dict(Adxsignature=hex(self.Adxsignature), dataOffset=self.dataOffset, Encoding=self.Encoding, Blocksize=self.Blocksize, SampleBitdepth=self.SampleBitdepth, channelCount=self.channelCount, SamplingRate=self.SamplingRate, SampleCount=self.SampleCount, HighpassFrequency=self.HighpassFrequency, AdxVersion=self.AdxVersion, Flags=self.Flags)
            return adx