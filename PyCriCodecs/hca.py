from io import BytesIO, FileIO
from struct import *
from .chunk import *
from typing import BinaryIO
from array import array
import CriCodecs

from .chunk import HCAType

HcaHeaderStruct = Struct(">4sHH")
HcaFmtHeaderStruct = Struct(">4sIIHH")
HcaCompHeaderStruct = Struct(">4sHBBBBBBBBBB")
HcaDecHeaderStruct = Struct(">4sHBBBBBB")
HcaLoopHeaderStruct = Struct(">4sIIHH")
HcaAthHeaderStruct = Struct(">4sH")
HcaVbrHeaderStruct = Struct(">4sHH")
HcaCiphHeaderStruct = Struct(">4sH")
HcaRvaHeaderStruct = Struct(">4sf")

""" CRC functions. """
hcacommon_crc_mask_table = [
    0x0000,0x8005,0x800F,0x000A,0x801B,0x001E,0x0014,0x8011,0x8033,0x0036,0x003C,0x8039,0x0028,0x802D,0x8027,0x0022,
    0x8063,0x0066,0x006C,0x8069,0x0078,0x807D,0x8077,0x0072,0x0050,0x8055,0x805F,0x005A,0x804B,0x004E,0x0044,0x8041,
    0x80C3,0x00C6,0x00CC,0x80C9,0x00D8,0x80DD,0x80D7,0x00D2,0x00F0,0x80F5,0x80FF,0x00FA,0x80EB,0x00EE,0x00E4,0x80E1,
    0x00A0,0x80A5,0x80AF,0x00AA,0x80BB,0x00BE,0x00B4,0x80B1,0x8093,0x0096,0x009C,0x8099,0x0088,0x808D,0x8087,0x0082,
    0x8183,0x0186,0x018C,0x8189,0x0198,0x819D,0x8197,0x0192,0x01B0,0x81B5,0x81BF,0x01BA,0x81AB,0x01AE,0x01A4,0x81A1,
    0x01E0,0x81E5,0x81EF,0x01EA,0x81FB,0x01FE,0x01F4,0x81F1,0x81D3,0x01D6,0x01DC,0x81D9,0x01C8,0x81CD,0x81C7,0x01C2,
    0x0140,0x8145,0x814F,0x014A,0x815B,0x015E,0x0154,0x8151,0x8173,0x0176,0x017C,0x8179,0x0168,0x816D,0x8167,0x0162,
    0x8123,0x0126,0x012C,0x8129,0x0138,0x813D,0x8137,0x0132,0x0110,0x8115,0x811F,0x011A,0x810B,0x010E,0x0104,0x8101,
    0x8303,0x0306,0x030C,0x8309,0x0318,0x831D,0x8317,0x0312,0x0330,0x8335,0x833F,0x033A,0x832B,0x032E,0x0324,0x8321,
    0x0360,0x8365,0x836F,0x036A,0x837B,0x037E,0x0374,0x8371,0x8353,0x0356,0x035C,0x8359,0x0348,0x834D,0x8347,0x0342,
    0x03C0,0x83C5,0x83CF,0x03CA,0x83DB,0x03DE,0x03D4,0x83D1,0x83F3,0x03F6,0x03FC,0x83F9,0x03E8,0x83ED,0x83E7,0x03E2,
    0x83A3,0x03A6,0x03AC,0x83A9,0x03B8,0x83BD,0x83B7,0x03B2,0x0390,0x8395,0x839F,0x039A,0x838B,0x038E,0x0384,0x8381,
    0x0280,0x8285,0x828F,0x028A,0x829B,0x029E,0x0294,0x8291,0x82B3,0x02B6,0x02BC,0x82B9,0x02A8,0x82AD,0x82A7,0x02A2,
    0x82E3,0x02E6,0x02EC,0x82E9,0x02F8,0x82FD,0x82F7,0x02F2,0x02D0,0x82D5,0x82DF,0x02DA,0x82CB,0x02CE,0x02C4,0x82C1,
    0x8243,0x0246,0x024C,0x8249,0x0258,0x825D,0x8257,0x0252,0x0270,0x8275,0x827F,0x027A,0x826B,0x026E,0x0264,0x8261,
    0x0220,0x8225,0x822F,0x022A,0x823B,0x023E,0x0234,0x8231,0x8213,0x0216,0x021C,0x8219,0x0208,0x820D,0x8207,0x0202,
]

def crc16_checksum(data):
    sum = 0
    for i in data:
        sum = ((sum << 8) & 0xFFFF) ^ (hcacommon_crc_mask_table[((sum >> 8) & 0xFFFF) ^ i] & 0xFFFF)
    return sum

class HCA:
    __slots__ = ["stream", "HcaSig", "version", "header_size", "key", "subkey", "hca", "filetype", "wavbytes",
                "riffSignature", "riffSize", "wave", "fmt", "fmtSize", "fmtType", "fmtChannelCount", "hcastream", 
                "fmtSamplingRate", "fmtSamplesPerSec", "fmtSamplingSize", "fmtBitCount", "dataSig", "dataSize", "Flags",
                "AlignmentSamples", "LoopCount", "LoopNum", "LoopType", "LoopStartSample", "LoopStartByte", "LoopEndSample", 
                "LoopEndByte", "looping", "hcabytes", "encrypted", "enc_table", "table"]
    stream: BinaryIO
    hcastream: BinaryIO
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
    encrypted: bool
    enc_table: array
    table: array
    looping: bool

    def __init__(self, stream: BinaryIO, key: int = 0, subkey: int = 0) -> None:
        if type(stream) == str:
            self.stream = FileIO(stream)
            self.hcastream = FileIO(stream)
        else:
            self.stream = BytesIO(stream)
            self.hcastream = BytesIO(stream)
        if type(key) == str:
            self.key = int(key, 16)
        else:
            self.key = key
        if type(subkey) == str:
            self.subkey = int(subkey, 16)
        else:
            self.subkey = subkey
        self.hcabytes: bytearray = b''
        self.enc_table: array = b''
        self.table: array = b''
        self.Pyparse_header()
    

    def Pyparse_header(self) -> None:
        self.HcaSig, self.version, self.header_size = HcaHeaderStruct.unpack(
            self.hcastream.read(HcaHeaderStruct.size)
        )
        if self.HcaSig in [HCAType.HCA.value, HCAType.EHCA.value]:
            if not self.hcabytes:
                self.filetype = "hca"
            if self.HcaSig == HCAType.EHCA.value:
                self.encrypted = True
            else:
                self.encrypted = False
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
            
            fmtsig, temp, framecount, encoder_delay, encoder_padding = HcaFmtHeaderStruct.unpack(
                self.hcastream.read(HcaFmtHeaderStruct.size)
            )
            channelcount = temp >> 24
            samplerate = temp & 0x00FFFFFF

            self.hca = dict(
                Encrypted = self.encrypted,
                Header=self.HcaSig,
                version=hex(self.version),
                HeaderSize=self.header_size,
                FmtSig = fmtsig,
                ChannelCount = channelcount,
                SampleRate = samplerate,
                FrameCount = framecount,
                EncoderDelay = encoder_delay,
                EncoderPadding = encoder_padding,
            )

            while True:
                sig = unpack(">I", self.hcastream.read(4))[0]
                self.hcastream.seek(-4, 1)
                sig = int.to_bytes(sig & 0x7F7F7F7F, 4, "big")
                if sig == b"comp":
                    compsig, framesize, minres, maxres, trackcount, channelconfig, totalbandcount, basebandcount, stereobandcount, bandsperhfrgroup, r1, r2 = HcaCompHeaderStruct.unpack(
                        self.hcastream.read(HcaCompHeaderStruct.size)
                    )
                    self.hca.update(
                        dict(
                            CompSig = compsig,
                            FrameSize = framesize,
                            MinResolution = minres,
                            MaxResolution = maxres,
                            TrackCount = trackcount,
                            ChannelConfig = channelconfig,
                            TotalBandCount = totalbandcount,
                            BaseBandCount = basebandcount, 
                            StereoBandCount = stereobandcount,
                            BandsPerHfrGroup = bandsperhfrgroup,
                            ReservedByte1 = r1,
                            ReservedByte2 = r2
                        )
                    )
                elif sig == b"ciph":
                    ciphsig, ciphertype = HcaCiphHeaderStruct.unpack(
                        self.hcastream.read(HcaCiphHeaderStruct.size)
                    )
                    if ciphertype == 1:
                        self.encrypted = True
                    self.hca.update(dict(CiphSig = ciphsig, CipherType = ciphertype))
                elif sig == b"loop":
                    self.looping = True
                    loopsig, loopstart, loopend, loopstartdelay, loopendpadding = HcaLoopHeaderStruct.unpack(
                        self.hcastream.read(HcaLoopHeaderStruct.size)
                    )
                    self.hca.update(dict(LoopSig = loopsig, LoopStart = loopstart, LoopEnd = loopend, LoopStartDelay = loopstartdelay, LoopEndPadding = loopendpadding))
                elif sig == b"dec\00":
                    decsig, framesize, maxres, minres, totalbandcount, basebandcount, temp, stereotype = HcaDecHeaderStruct.unpack(
                        self.hcastream.read(HcaDecHeaderStruct.size)
                    )
                    trackcount = temp >> 4
                    channelconfig = temp & 0xF
                    self.hca.update(
                        dict(
                            DecSig = decsig,
                            FrameSize = framesize,
                            MinResolution = minres,
                            MaxResolution = maxres,
                            TotalBandCount = totalbandcount,
                            BaseBandCoung = basebandcount, 
                            TrackCount = trackcount,
                            ChannelConfig = channelconfig,
                            StereoType = stereotype
                        )
                    )
                elif sig == b"ath\00":
                    athsig, tabletype = HcaAthHeaderStruct.unpack(
                        self.hcastream.read(HcaAthHeaderStruct.size)
                    )
                    self.hca.update(dict(AthSig = athsig, TableType = tabletype))
                elif sig == b"vbr\00":
                    vbrsig, maxframesize, noiselevel = HcaVbrHeaderStruct.unpack(
                        self.hcastream.read(HcaVbrHeaderStruct.size)
                    )
                    self.hca.update(dict(VbrSig = vbrsig, MaxFrameSize = maxframesize, NoiseLevel = noiselevel))
                elif sig == b"rva\00":
                    rvasig, volume = HcaRvaHeaderStruct.unpack(
                        self.hcastream.read(HcaRvaHeaderStruct.size)
                    )
                    self.hca.update(dict(RvaSig = rvasig, Volume = volume))
                else:
                    break
            Crc16 = self.hcastream.read(2)
            self.hca.update(dict(Crc16 = Crc16))

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
        else:
            raise ValueError("Invalid HCA or WAV file.")
        self.stream.seek(0)
        self.hcastream.seek(0)
    
    def info(self) -> dict:
        """ Returns info related to the input file. """
        if self.filetype == "hca":
            return self.hca
        elif self.filetype == "wav":
            wav = dict(RiffSignature=self.riffSignature.decode(), riffSize=self.riffSize, WaveSignature=self.wave.decode(), fmtSignature=self.fmt.decode(), fmtSize=self.fmtSize, fmtType=self.fmtType, fmtChannelCount=self.fmtChannelCount, fmtSamplingRate=self.fmtSamplingRate, fmtSamplesPerSec=self.fmtSamplesPerSec, fmtSamplingSize=self.fmtSamplingSize, fmtBitCount=self.fmtBitCount, dataSignature=self.dataSig.decode(), dataSize=self.dataSize)
            return wav
    
    def decode(self) -> bytes:
        if self.filetype == "wav":
            raise ValueError("Input type for decoding must be an HCA file.")
        self.hcastream.seek(0)
        self.wavbytes = CriCodecs.HcaDecode(self.hcastream.read(), self.header_size, self.key, self.subkey)
        self.stream = BytesIO(self.wavbytes)
        self.hcastream.seek(0)
        return bytes(self.wavbytes)
    
    def encode(self, force_not_looping: bool = False, encrypt: bool = False, keyless: bool = False) -> bytes:
        if self.filetype == "hca":
            raise ValueError("Input type for encoding must be a WAV file.")
        if force_not_looping == False:
            force_not_looping = 0
        elif force_not_looping == True:
            force_not_looping = 1
        else:
            raise ValueError("Forcing the encoder to not loop is by either False or True.")
        self.stream.seek(0)
        self.hcabytes = CriCodecs.HcaEncode(self.stream.read(), force_not_looping)
        self.hcastream = BytesIO(self.hcabytes)
        self.Pyparse_header()
        if encrypt:
            if self.key == 0 and not keyless:
                self.key = 0xCF222F1FE0748978 # Default key.
            self.encrypt(self.key, keyless)
        return self.get_hca()
    
    def write_header(self, keyless: bool = False) -> bytearray:
        if self.encrypted:
            mask = 0x7F7F7F7F
            self.hca["CipherType"] = 0
        else:
            mask = 0xFFFFFFFF
            if keyless:
                mask = 0x7F7F7F7F
                self.hca["CipherType"] = 1
            else:
                self.hca["CipherType"] = 56

        obfhca  = 0xC8C3C100
        obffmt  = 0xE6EDF400
        obfciph = 0xE3E9F0E8
        obfloop = 0xECEFEFF0
        obfrva  = 0xF2F6C100
        obfvbr  = 0xF6E2F200
        obfdec  = 0xE4E5E300
        obfath  = 0xE1F4E800
        obfcomp = 0xE3EFEDF0
        obfpad  = 0xF0E1E400

        header = bytearray()

        """ HCA """
        header += HcaHeaderStruct.pack(int.to_bytes(obfhca & mask, 4, "big"), self.version, self.header_size)

        """ FMT """
        temp = (self.hca["ChannelCount"] << 24) | (self.hca["SampleRate"])
        header += HcaFmtHeaderStruct.pack(
            int.to_bytes(obffmt & mask, 4, "big"),
            temp,
            self.hca["FrameCount"],
            self.hca["EncoderDelay"],
            self.hca["EncoderPadding"]
            )

        """ COMP """
        if "CompSig" in self.hca:
            header += HcaCompHeaderStruct.pack(
                int.to_bytes(obfcomp & mask, 4, "big"),
                self.hca["FrameSize"],
                self.hca['MinResolution'],
                self.hca['MaxResolution'],
                self.hca['TrackCount'],
                self.hca['ChannelConfig'],
                self.hca["TotalBandCount"],
                self.hca["BaseBandCount"],
                self.hca["BandsPerHfrGroup"],
                self.hca["StereoBandCount"],
                self.hca["ReservedByte1"],
                self.hca["ReservedByte2"]
                )
        
        """ DEC """
        if "DecSig" in self.hca:
            temp = (self.hca['TrackCount'] << 4) | (self.hca['ChannelConfig'])
            header += HcaDecHeaderStruct.pack(
                int.to_bytes(obfdec & mask, 4, "big"),
                self.hca["FrameSize"],
                self.hca['MinResolution'],
                self.hca['MazResolution'],
                self.hca["TotalBandCount"],
                self.hca["BaseBoundCount"],
                temp,
                self.hca["StereoType"]
                )
        
        """ VBR """
        if "VbrSig" in self.hca:
            header += HcaVbrHeaderStruct.pack(
                int.to_bytes(obfvbr & mask, 4, "big"),
                self.hca["MaxFrameSize"],
                self.hca["NoiseLevel"]
            )

        """ ATH """
        if "AthSig" in self.hca:
            header += HcaAthHeaderStruct.pack(
                int.to_bytes(obfath & mask, 4, "big"),
                self.hca["AthTableType"]
            )
        
        """ LOOP """
        if "LoopSig" in self.hca and self.looping:
            header += HcaLoopHeaderStruct.pack(
                int.to_bytes(obfloop & mask, 4, "big"),
                self.hca["LoopStart"],
                self.hca["LoopEnd"],
                self.hca["LoopStartDelay"],
                self.hca["LoopEndPadding"]
            )

        """ CIPH """
        if "CiphSig" in self.hca:
            header += HcaCiphHeaderStruct.pack(
                int.to_bytes(obfciph & mask, 4, "big"),
                self.hca["CipherType"]
            )
        
        """ RVA """
        if "RvaSig" in self.hca:
            header += HcaRvaHeaderStruct.pack(
                int.to_bytes(obfrva & mask, 4, "big"),
                self.hca['Volume']
            )

        if self.header_size > len(header):
            header += int.to_bytes(obfpad & mask, 4, "big")
            header += bytearray(self.header_size - len(header) - 2)
            crc_sum = int.to_bytes(crc16_checksum(header), 2, "big")
            header += crc_sum
        
        return header

    # Although I made a sole pythonic way someday before, now I lost it, so I will take it from
    # VGMStream yet again.
    def cipher_init1(self, cipher_table: array):
        mul = 13
        add = 11
        v = 0
        for i in range(1, 255):
            v = (v * mul + add) & 0xFF
            if(v == 0 or v == 0xFF):
                v = (v * mul + add) & 0xFF
            cipher_table[i] = v
        cipher_table[0] = 0
        cipher_table[0xFF] = 0xFF
    
    def cipher_init56_create_table(self, r: array, key: int):
        mul = ((key & 1) << 3) | 5
        add = (key & 0xE) | 1

        key >>= 4
        for i in range(16):
            key = (key * mul + add) & 0xF
            r[i] = key

    def cipher_init56(self, cipher_table, keycode):
        kc = array("B", bytearray(8))
        base_r = array("B", bytearray(16))
        base_c = array("B", bytearray(16))
        seed = array("B", bytearray(16))
        base = array("B", bytearray(256))

        if (keycode != 0):
            keycode -= 1

        for r in range(7):
            kc[r] = keycode & 0xFF
            keycode = keycode >> 8
    
        seed[0x00] = kc[1]
        seed[0x01] = kc[1] ^ kc[6]
        seed[0x02] = kc[2] ^ kc[3]
        seed[0x03] = kc[2]
        seed[0x04] = kc[2] ^ kc[1]
        seed[0x05] = kc[3] ^ kc[4]
        seed[0x06] = kc[3]
        seed[0x07] = kc[3] ^ kc[2]
        seed[0x08] = kc[4] ^ kc[5]
        seed[0x09] = kc[4]
        seed[0x0A] = kc[4] ^ kc[3]
        seed[0x0B] = kc[5] ^ kc[6]
        seed[0x0C] = kc[5]
        seed[0x0D] = kc[5] ^ kc[4]
        seed[0x0E] = kc[6] ^ kc[1]
        seed[0x0F] = kc[6]

        self.cipher_init56_create_table(base_r, kc[0])
        for r in range(16):
            self.cipher_init56_create_table(base_c, seed[r])
            nb = (base_r[r] << 4) & 0xFF
            for c in range(16):
                base[r*16 + c] = nb | base_c[c]

        x = 0
        pos = 1
        for i in range(256):
            x = (x + 17) & 0xFF
            if (base[x] != 0 and base[x] != 0xFF):
                cipher_table[pos] = base[x]
                pos += 1
        cipher_table[0] = 0
        cipher_table[0xFF] = 0xFF

    def generate_table(self, keycode: int) -> None:
        if self.table and self.enc_table:
            # To avoid making the table again.
            return
        table = array("B", bytearray(0x100))
        if self.hca['CipherType'] == 1:
            # Static key.
            self.cipher_init1(table)
        else:
            self.cipher_init56(table, keycode)
        
        self.table = table
        enc_table = array("B", bytearray(0x100))
        for i in range(0x100):
            enc_table[table[i]] = i
        self.enc_table = enc_table
            
    def encrypt(self, keycode: int, keyless: bool = False) -> None:
        """ Encrypts an HCA file with the given keycode, if they encryption is set to keyless, the keycode will be ignored. """
        # This will encrypt the HCA file with the given keycode.
        if not (self.filetype == "hca" or (self.hcabytes and self.hcastream)):
            raise ValueError("Encrypt function is only for HCA files. You can call this function after encoding.")
        elif self.encrypted:
            raise ValueError("Provided HCA file is already encrypted.")
        Frames = bytearray()
        Frames += self.write_header(keyless)
        self.generate_table(keycode)
        self.hcastream.seek(self.header_size)
        for i in range(self.hca['FrameCount']):
            fbytes = memoryview(bytearray(self.hcastream.read(self.hca['FrameSize'])))
            for j in range(self.hca['FrameSize'] - 2):
                fbytes[j] = self.enc_table[fbytes[j]]
            fbytes = bytearray(fbytes)
            crc_sum = int.to_bytes(crc16_checksum(fbytes[:-2]), 2, "big")
            fbytes[-2] = crc_sum[0]
            fbytes[-1] = crc_sum[1]
            Frames += fbytes
        
        self.hcastream.seek(0)
        self.encrypted = True
        self.hcastream = BytesIO(Frames)
        self.hcastream.seek(0)

    def decrypt(self) -> None:
        """ Decrypts an HCA file with the provided key in initialization. """
        # This will use the init HCA values to decrypt.
        if not (self.filetype == "hca" or (self.hcabytes and self.hcastream)):
            raise ValueError("Decrypt function is only used for HCA files.")
        elif not self.encrypted:
            raise ValueError("Provided HCA file is not encrypted.")
        Frames = bytearray()
        Frames += self.write_header()
        keycode = self.key
        if self.subkey:
            keycode = keycode * ((self.subkey << 16) | ((~self.subkey) + 2))
        self.generate_table(keycode)
        self.hcastream.seek(self.header_size, 0)
        for i in range(self.hca['FrameCount']):
            fbytes = memoryview(bytearray(self.hcastream.read(self.hca['FrameSize'])))
            for j in range(self.hca['FrameSize'] - 2):
                fbytes[j] = self.table[fbytes[j]]
            fbytes = bytearray(fbytes)
            crc_sum = int.to_bytes(crc16_checksum(fbytes[:-2]), 2, "big")
            fbytes[-2] = crc_sum[0]
            fbytes[-1] = crc_sum[1]
            Frames += fbytes
        
        self.hcastream.seek(0)
        self.encrypted = False
        self.hcastream = BytesIO(Frames)
        self.hcastream.seek(0)

    def get_hca(self) -> bytes:
        """ Use this function to get the HCA file bytes after encrypting or decrypting. """
        self.hcastream.seek(0)
        fl: bytes = self.hcastream.read()
        self.hcastream.seek(0)
        return fl
    
    def get_frames(self):
        """ Generator function to yield Frame number, and Frame data. """
        self.stream.seek(self.header_size, 0)
        for i in range(self.hca['FrameCount']):
            yield (i, self.hcastream.read(self.hca['FrameSize']))
