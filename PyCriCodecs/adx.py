from io import FileIO
from struct import *
import CriCodecs

AdxHeaderStruct = Struct(">HHBBBBIIHBB")
#AdxHeaderStruct = Struct(">HHBBBBIIHBBHHIIIIIIII")
WavHeaderStruct = Struct("<4sI4s4sIHHIIHH4sI")

class ADX:
    __slots__ = ["filename", "filetype", "wavStream", "sfaStream", "Adxsignature", "dataOffset", "Encoding",
                "Blocksize", "SampleBitdepth", "channelCount", "SamplingRate", "SampleCount", "HighpassFrequency",
                "AdxVersion", "riffSignature", "riffSize", "wave", "fmt", "fmtSize", "fmtType", "fmtChannelCount", 
                "fmtSamplingRate", "fmtSamplesPerSec", "fmtSamplingSize", "fmtBitCount", "dataSig", "dataSize", "Flags"]
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

    def __init__(self, filename=False, filetype=None) -> None:
        if not filename:
            raise ValueError("No filename given or the file doesn't exist.")
        self.filename = filename
        if filetype == "wav":
            self.wavStream = open(filename, "rb")
            self.filetype = "wav"
            self.load_info()
        elif filetype == "adx":
            self.sfaStream = open(filename, "rb")
            self.filetype = "adx"
            self.load_info()
        else:
            raise ValueError("No filetype specified for given file. Either 'wav' or 'adx' format.")

    def load_info(self) -> None:
        if self.filetype == "wav":
            self.riffSignature, self.riffSize, self.wave, self.fmt, self.fmtSize, self.fmtType, self.fmtChannelCount, self.fmtSamplingRate, self.fmtSamplesPerSec, self.fmtSamplingSize, self.fmtBitCount, self.dataSig, self.dataSize = WavHeaderStruct.unpack(
            self.wavStream.read(WavHeaderStruct.size)
            )
            self.wavStream.close()
            if self.riffSignature == b"RIFF" and self.wave == b'WAVE' and self.fmt == b'fmt ' and self.dataSig == b'data':
                return
            else:
                raise ValueError("Invalid wav file.")
        elif self.filetype == "adx":
            self.Adxsignature, self.dataOffset, self.Encoding, self.Blocksize, self.SampleBitdepth, self.channelCount, self.SamplingRate, self.SampleCount, self.HighpassFrequency, self.AdxVersion, self.Flags = AdxHeaderStruct.unpack(
            self.sfaStream.read(AdxHeaderStruct.size)
            )
            self.sfaStream.close()
            if self.Adxsignature == 0x8000:
                # TODO Add loading looping info per version.
                # if self.AdxVersion == 3:
                #     self.LoopAlignmentSamples, LoopEnabled, LoopEnabled_, LoopBeginSampleIndex, LoopBeginByteIndex, LoopEndSampleIndex, LoopEndByteIndex = unpack(
                #         "HHIIIII", self.sfaStream.read(calcsize("HHIIIII"))
                #     )
                # if self.AdxVersion == 4:
                #     self.... = unpack(
                #         "HHIIIIIIII", self.sfaStream.read(calcsize("HHIIIIIIII"))
                #     )
                return
            else:
                raise ValueError("Invalid adx file.")



    # Decodes ADX to WAV.
    def decode(self) -> bytearray:

        if self.filetype != "adx":
            raise ValueError("Not an ADX file")
        elif self.Adxsignature != 0x8000:
            raise ValueError("Not an ADX file")
        elif not (self.Encoding == 3 or self.Encoding == 4 or self.Encoding == 2):
            raise NotImplementedError("Unsupported ADX Encoding.")
        elif self.SampleBitdepth != 4:
            raise NotImplementedError("Unsupported Bitdepth.")

        fmtChannelCount = self.channelCount
        fmtSamplingRate = self.SamplingRate
        fmtSamplingSize = fmtChannelCount * 2
        fmtSamplesPerSec = fmtSamplingRate * fmtSamplingSize
        dataSize = self.SampleCount * fmtSamplingSize
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
                                        b"data",
                                        dataSize
                                        )
        
        outfile = bytearray()
        outfile.extend(WavHeader)
        outfile.extend(CriCodecs.AdxDecode(self.filename))
        return outfile
            
    # Encodes WAV to ADX.
    def encode(self, Blocksize = 0x12, AdxVersion = 0x4, DataOffset = 0x011C) -> bytearray:

        if self.filetype != "wav":
            raise ValueError("Not a WAV file or an unsupported file version.")
        elif self.riffSignature != b"RIFF" and self.wave != b'WAVE' and self.fmt != b'fmt ' and self.dataSig != b'data':
            raise ValueError("Not a WAV file or an unsupported file version.")
        
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
                        Blocksize-4 # 0xE, Padding size? My conclusion here is that it's (blocksize - 4)
                        ) + bytearray(Blocksize-4)
        
        outfile = bytearray()
        outfile.extend(AdxHeader)
        # It's good to standardize the header data size before the audio data begins.
        # This way we can split it into SFA chunks. In this case, 0x120 is the most common size I saw.
        # It also helps adding support for looping information in the header. (How big?)
        outfile += bytearray(DataOffset-22)
        outfile.extend(b'(c)CRI')
        outfile.extend(CriCodecs.AdxEncode(self.filename, Blocksize))
        outfile.extend(AdxFooter)
        return outfile

    
    def info(self) -> dict:
        if self.filetype == "wav":
            wav = dict(RiffSignature=self.riffSignature.decode(), riffSize=self.riffSize, WaveSignature=self.wave.decode(), fmtSignature=self.fmt.decode(), fmtSize=self.fmtSize, fmtType=self.fmtType, fmtChannelCount=self.fmtChannelCount, fmtSamplingRate=self.fmtSamplingRate, fmtSamplesPerSec=self.fmtSamplesPerSec, fmtSamplingSize=self.fmtSamplingSize, fmtBitCount=self.fmtBitCount, dataSignature=self.dataSig.decode(), dataSize=self.dataSize)
            return wav
        elif self.filetype == "adx":
            adx = dict(Adxsignature=hex(self.Adxsignature), dataOffset=self.dataOffset, Encoding=self.Encoding, Blocksize=self.Blocksize, SampleBitdepth=self.SampleBitdepth, channelCount=self.channelCount, SamplingRate=self.SamplingRate, SampleCount=self.SampleCount, HighpassFrequency=self.HighpassFrequency, AdxVersion=self.AdxVersion, Flags=self.Flags)
            return adx