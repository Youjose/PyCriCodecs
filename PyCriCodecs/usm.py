from typing import BinaryIO
from .chunk import *
from .utf import UTF
from io import FileIO, BytesIO

# Big thanks and credit for k0lb3 and 9th helping me write this specific code.
# Also credit for the original C++ code from Nyagamon/bnnm.

# Apparently there is an older USM format called SofDec? This is for SofDec2 though.
# Extraction working only for now, although check https://github.com/donmai-me/WannaCRI/
# code for a complete breakdown of the USM format.
class USM:
    __slots__ = ["filename", "videomask1", "videomask2", "audiomask", "decrypt", 
                "DecryptAudio", "Videodata", "Audiodata", "stream_id", "stream", "CRIDTable"]
    filename: BinaryIO
    videomask1: bytearray
    videomask2: bytearray
    audiomask: bytearray
    decrypt: bool
    DecryptAudio: bool
    Videodata: bytes
    Audiodata: bytes
    stream_id: int
    stream: BinaryIO

    """
    Sets the decryption status, if the key is not given, it will return the plain SFV data.
    If the key is given you can set whether the Audio should be encrypted or not by (DecryptAudio = True),
    it's False by default.
    """
    def __init__(self, filename, key: bool = False, DecryptAudio: bool = False):
        self.filename = filename
        self.DecryptAudio = DecryptAudio
        self.decrypt = False
        self.Videodata = None
        self.Audiodata = None
        self.stream_id = 0

        
        if key and DecryptAudio or key and not DecryptAudio:
            self.decrypt = True
            self.init_key(key)
        if not key and DecryptAudio:
            raise Exception("Cannot decrypt Audio without a key")
        else:
            self.load_file()
    
    # Initialized the key masks, can return the initial mask with (returnmask = true)
    def init_key(self, key: str):
        key1 = bytes.fromhex(key[8:])
        key2 = bytes.fromhex(key[:8])
        t = bytearray(0x20)
        t[0x00:0x09] = [
            key1[3],
            key1[2],
            key1[1],
            (key1[0] - 0x34) % 0x100,
            (key2[3] + 0xF9) % 0x100,
            (key2[2] ^ 0x13) % 0x100,
            (key2[1] + 0x61) % 0x100,
            (key1[3] ^ 0xFF) % 0x100,
            (key1[1] + key1[2]) % 0x100,
        ]
        t[0x09:0x0C] = [
            (t[0x01] - t[0x07]) % 0x100,
            (t[0x02] ^ 0xFF) % 0x100,
            (t[0x01] ^ 0xFF) % 0x100,
        ]
        t[0x0C:0x0E] = [
            (t[0x0B] + t[0x09]) % 0x100,
            (t[0x08] - t[0x03]) % 0x100,
        ]
        t[0x0E:0x10] = [
            (t[0x0D] ^ 0xFF) % 0x100,
            (t[0x0A] - t[0x0B]) % 0x100,
        ]
        t[0x10] = ((t[0x08] - t[0x0F]) % 0x100)
        t[0x11:0x17] = [
            (t[0x10] ^ t[0x07]) % 0x100,
            (t[0x0F] ^ 0xFF) % 0x100,
            (t[0x03] ^ 0x10) % 0x100,
            (t[0x04] - 0x32) % 0x100,
            (t[0x05] + 0xED) % 0x100,
            (t[0x06] ^ 0xF3) % 0x100,
        ]
        t[0x17:0x1A] = [
            (t[0x13] - t[0x0F]) % 0x100,
            (t[0x15] + t[0x07]) % 0x100,
            (0x21 - t[0x13]) % 0x100,
        ]
        t[0x1A:0x1C] = [
            (t[0x14] ^ t[0x17]) % 0x100,
            (t[0x16] + t[0x16]) % 0x100,
        ]
        t[0x1C:0x1F] = [
            (t[0x17] + 0x44) % 0x100,
            (t[0x03] + t[0x04]) % 0x100,
            (t[0x05] - t[0x16]) % 0x100,
        ]
        t[0x1F] = (t[0x1D] ^ t[0x13]) % 0x100
        t2=[b'U', b'R', b'U', b'C']
        self.videomask1 = t
        self.videomask2 = bytearray(map(lambda x: x ^ 0xFF, t))
        self.audiomask = bytearray(0x20)
        for x in range(0x20):
            if (x&1) == 1:
                self.audiomask[x] = ord(t2[(x>>1)&3])
            else:
                self.audiomask[x] = self.videomask2[x]
    
    # Loads in the file and check if it's an USM file.
    def load_file(self):
        if type(self.filename) == str:
            self.stream = FileIO(self.filename)
        else:
            self.stream = BytesIO(self.filename)
        self.stream.seek(0, 2)
        size = self.stream.tell()
        self.stream.seek(0)
        header = self.stream.read(4)
        if header != USMChunckHeaderType.CRID.value:
            raise ValueError(f"Unsupported file type: {self.header}")
        self.stream.seek(0)
        self.demux(size)
    
    # Demuxes the USM
    def demux(self, size: int):

        video_output = []
        while self.stream.tell() < size:
            header, chuncksize, unk08, offset, padding, chno, unk0D, unk0E, type, frametime, framerate, unk18, unk1C = USMChunkHeader.unpack(
                self.stream.read(USMChunkHeader.size)
            )
            chuncksize -= 0x18
            offset -= 0x18
            if header == USMChunckHeaderType.CRID.value:
                audio_output = [bytearray()] * int(chno+1)
                self.stream_id = int(chno+1)
                self.CRIDTable = UTF(self.stream.read(chuncksize)).table
            elif header == USMChunckHeaderType.SFV.value:
                # SFV chunk
                if type == 0:
                    data = bytearray(self.stream.read(chuncksize)[offset:])
                    if padding:
                        video_output.append((self.VideoMask(data))[:-padding] if self.decrypt else data[:-padding])
                    else:
                        video_output.append((self.VideoMask(data)) if self.decrypt else data)
                elif type == 1 or type == 3:
                    SFVTable = UTF(self.stream.read(chuncksize)).table
                else:
                    self.stream.seek(chuncksize, 1)
            elif header == USMChunckHeaderType.SFA.value:
                # SFA chunk
                if type == 0:
                    data = bytearray(self.stream.read(chuncksize)[offset:])
                    if padding:
                        (audio_output[int(chno)]).extend((self.AudioMask(data))[:-padding] if self.DecryptAudio else data[:-padding])
                    else:
                        (audio_output[int(chno)]).extend((self.AudioMask(data)) if self.DecryptAudio else data)
                elif type == 1 or type == 3:
                    SFATable = UTF(self.stream.read(chuncksize)).table
                else:
                    self.stream.seek(chuncksize, 1)
            elif header == USMChunckHeaderType.ALP.value:
                # ALP chunk, unsupported atm.
                self.stream.seek(chuncksize, 1)
            else:
                raise NotImplementedError(f"Unsupported chunk type: {header}")

        self.Videodata = b"".join(video_output)
        self.Audiodata = audio_output

    # Decrypt SFV chunks, should only be used if the video data is encrypted.
    def VideoMask(self, memObj: bytearray) -> bytearray:
        head = memObj[:0x40]
        memObj = memObj[0x40:]
        size = len(memObj)
        # memObj len is a cached property, very fast to lookup
        if size <= 0x200:
            return (head + memObj)
        data_view = memoryview(memObj).cast("Q")

        # mask 2
        mask = bytearray(self.videomask2)
        mask_view = memoryview(mask).cast("Q")
        vmask = self.videomask2
        vmask_view = memoryview(vmask).cast("Q")

        mask_index = 0

        for i in range(32, size // 8):
            data_view[i] ^= mask_view[mask_index]
            mask_view[mask_index] = data_view[i] ^ vmask_view[mask_index]
            mask_index = (mask_index + 1) % 4

        # mask 1
        mask = bytearray(self.videomask1)
        mask_view = memoryview(mask).cast("Q")
        mask_index = 0
        for i in range(32):
            mask_view[mask_index] ^= data_view[i + 32]
            data_view[i] ^= mask_view[mask_index]
            mask_index = (mask_index + 1) % 4

        return (head + memObj)

    # Decrypts SFA chunks, should just be used with ADX files.
    def AudioMask(self, memObj: bytearray) -> bytearray:
        head = memObj[:0x140]
        memObj = memObj[0x140:]
        size = len(memObj)
        data_view = memoryview(memObj).cast("Q")
        mask = bytearray(self.audiomask)
        mask_view = memoryview(mask).cast("Q")
        for i in range(size//8):
            data_view[i] ^= mask_view[i%4]
        return (head + memObj)