import os
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
    """ USM class for extracting infromation and data from a USM file. """
    __slots__ = ["filename", "videomask1", "videomask2", "audiomask", "decrypt", 
                "stream", "__fileinfo", "CRIDObj", "size", "output", "codec"]
    filename: BinaryIO
    videomask1: bytearray
    videomask2: bytearray
    audiomask: bytearray
    decrypt: bool
    DecryptAudio: bool
    stream: BinaryIO
    __fileinfo: list[dict]
    CRIDObj: UTF
    output: dict[str, list[bytearray]]
    size: int
    codec: int

    def __init__(self, filename, key: str = False):
        """
        Sets the decryption status, if the key is not given, it will return the plain SFV data.
        If the key is given the code will decrypt SFA data if it was ADX, otherwise return plain SFA data.
        """
        self.filename = filename
        self.decrypt = False

        
        if key:
            self.decrypt = True
            self.init_key(key)
        self.load_file()
    
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
        self.size = self.stream.tell()
        self.stream.seek(0)
        header = self.stream.read(4)
        if header != USMChunckHeaderType.CRID.value:
            raise NotImplementedError(f"Unsupported file type: {self.header}")
        self.stream.seek(0)
    
    # Demuxes the USM
    def demux(self) -> None:
        """ Gets data from USM chunks and assignes them to output. """
        self.stream.seek(0)
        self.__fileinfo = list() # Prototype, should be improved.
        header, chuncksize, unk08, offset, padding, chno, unk0D, unk0E, type, frametime, framerate, unk18, unk1C = USMChunkHeader.unpack(
            self.stream.read(USMChunkHeader.size)
        )
        chuncksize -= 0x18
        offset -= 0x18
        self.CRIDObj = UTF(self.stream.read(chuncksize))
        CRID_payload = self.CRIDObj.get_payload()
        self.__fileinfo.append({self.CRIDObj.table_name: CRID_payload})
        headers = [(int.to_bytes(x['stmid'][1], 4, "big")).decode() for x in CRID_payload[1:]]
        chnos = [x['chno'][1] for x in CRID_payload[1:]]
        output: dict[str, list[bytearray]]
        output = dict()
        for i in range(len(headers)):
            output[headers[i]] = [bytearray()] * (chnos[i]+1)
        while self.stream.tell() < self.size:
            header: bytes
            header, chuncksize, unk08, offset, padding, chno, unk0D, unk0E, type, frametime, framerate, unk18, unk1C = USMChunkHeader.unpack(
                self.stream.read(USMChunkHeader.size)
            )
            chuncksize -= 0x18
            offset -= 0x18
            if header.decode() in headers:
                if type == 0:
                    data = self.reader(chuncksize, offset, padding, header)
                    output[header.decode()][chno].extend(data)
                elif type == 1 or type == 3:
                    ChunkObj = UTF(self.stream.read(chuncksize))
                    self.__fileinfo.append({ChunkObj.table_name: ChunkObj.get_payload()})
                    if type == 1 and header == USMChunckHeaderType.SFA.value:
                        codec = ChunkObj.get_payload()[0]
                        self.codec = codec['audio_codec'][1] # So far, audio_codec of 2, means ADX, while audio_codec 4 means HCA.
                else:
                    self.stream.seek(chuncksize, 1)
            else:
                # It is likely impossible for the code to reach here, since the code right now is suitable
                # for any chunk type specified in the CRID header.
                # But just incase somehow there's an extra chunk, this code might handle it.
                if header in [chunk.value for chunk in USMChunckHeaderType]:
                    if type == 0:
                        output[header.decode()] = [bytearray()]
                        data = self.reader(chuncksize, offset, padding, header)
                        output[header.decode()][0].extend(data) # No channel number info, code here assumes it's a one channel data type.
                    elif type == 1 or type == 3:
                        ChunkObj = UTF(self.stream.read(chuncksize))
                        self.__fileinfo.append({ChunkObj.table_name: ChunkObj.get_payload()})
                        if type == 1 and header == USMChunckHeaderType.SFA.value:
                            codec = ChunkObj.get_payload()[0]
                            self.codec = codec['audio_codec'][1]
                    else:
                        self.stream.seek(chuncksize, 1)
                else:
                    raise NotImplementedError(f"Unsupported chunk type: {header}")
        self.output = output

    def extract(self, dirname: str = ""):
        """ Extracts all USM contents. """
        self.stream.seek(0)
        self.demux()
        table = self.CRIDObj.get_payload()
        point = 0 # My method is not ideal here, but it'll hopefully work.
        dirname = dirname # You can add a directory where all extracted data goes into.
        filenames = []
        for i in table[1:]: # Skips the CRID table since it has no actual file.
            filename: str = i['filename'][1]

            # Adjust filenames and/or paths to extract them into the current directory.
            if ":\\" in filename: # Absolute paths.
                filename = filename.split(":\\", 1)[1]
            elif ":/" in filename: # Absolute paths.
                filename = filename.split(":/", 1)[1]
            elif ":"+os.sep in filename: # Absolute paths.
                filename = filename.split(":"+os.sep, 1)[1]
            elif ".."+os.sep in filename: # Relative paths.
                filename = filename.rsplit(".."+os.sep, 1)[1]
            elif "../" in filename: # Relative paths.
                filename = filename.rsplit("../", 1)[1]
            elif "..\\" in filename: # Relative paths.
                filename = filename.rsplit("..\\", 1)[1]
            filename = ''.join(x for x in filename if x not in ':?*<>|"') # removes illegal characters.
            
            filename = os.path.join(dirname, filename) # Preserves the path structure if there's one.
            if filename not in filenames:
                filenames.append(filename)
            else:
                if "." in filename:
                    fl = filename.rsplit(".", 1)
                    filenames.append(fl[0] + "_" + str(point) + "." + fl[1])
                    point += 1
                else:
                    filenames.append(filename + "_" + str(point))
                    point += 1
        
        point = 0
        for chunk in self.output:
            for data in self.output[chunk]:
                if dirname or "\\" in filenames[point] or "/" in filenames[point] or os.sep in filenames[point]:
                    os.makedirs(os.path.dirname(filenames[point]), exist_ok=True)
                if chunk == "@SBT":
                    texts = self.sbt_to_srt(data)
                    for i in range(len(texts)):
                        filename = filenames[point]
                        if "." in filename:
                            fl = filename.rsplit(".", 1)
                            filename = fl[0] + "_" + str(i) + ".srt"
                        else:
                            filename = filename + "_" + str(i)
                        open(filename, "w", encoding="utf-8").write(texts[i])
                    else:
                        open(filenames[point], "wb").write(data)
                        point += 1
                else:
                    open(filenames[point], "wb").write(data)
                    point += 1

    def reader(self, chuncksize, offset, padding, header) -> bytearray:
        """ Chunks reader function, reads all data in a chunk and returns a bytearray. """
        data = bytearray(self.stream.read(chuncksize)[offset:])
        if header == USMChunckHeaderType.SFV.value or header == USMChunckHeaderType.ALP.value:
            data = self.VideoMask(data) if self.decrypt else data
        elif header == USMChunckHeaderType.SFA.value:
            data = self.AudioMask(data) if (self.codec == 2 and self.decrypt) else data
        if padding:
            data = data[:-padding]
        return data

    # Decrypt SFV chunks or ALP chunks, should only be used if the video data is encrypted.
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
    
    def sbt_to_srt(self, stream: bytearray) -> list:
        """ Convert SBT chunks info to SRT. """
        # Since I got no idea what's the origin of the SBT format, I am just going to convert it to SRT.
        size = len(stream)
        stream: BytesIO = BytesIO(stream)
        out: dict[int, list[str]]
        out = dict()
        while stream.tell() < size:
            langid, framerate, frametime, duration, data_size = SBTChunkHeader.unpack(
                stream.read(SBTChunkHeader.size)
            )
            # Language ID's are arbitrary, so they could be anything.
            duration_in_ms = frametime
            ms   = duration_in_ms % framerate
            sec  = (duration_in_ms // framerate) % 60
            mins = (duration_in_ms // (framerate*60)) % 60
            hrs  = (duration_in_ms // (framerate*60*60)) % 24
            start = f'{hrs:0>2.0f}:{mins:0>2.0f}:{sec:0>2.0f},{ms:0>3.0f}'

            duration_in_ms = frametime + duration
            ms   = duration_in_ms % framerate
            sec  = (duration_in_ms // framerate) % 60
            mins = (duration_in_ms // (framerate*60)) % 60
            hrs  = (duration_in_ms // (framerate*60*60)) % 24
            end = f'{hrs:0>2.0f}:{mins:0>2.0f}:{sec:0>2.0f},{ms:0>3.0f}'

            text = stream.read(data_size)
            if text.endswith(b"\x00\x00"):
                text = text[:-2].decode("utf-8", errors="ignore") + "\n\n"
            else:
                text = text.decode("utf-8", errors="ignore")
            if langid in out:
                out[langid].append(str(int(out[langid][-1].split("\n", 1)[0]) + 1) + "\n" + start + " --> " + end + "\n" + text)
            else:
                out[langid] = [(str(1) + "\n" + start + " --> " + end + "\n" + text)]
        out = ["".join(v) for k, v in out.items()]
        return out
    
    def get_metadata(self):
        return self.__fileinfo

# TODO Before touching on this, there are a lot of unknowns, minbuf(minimum buffer of what?) and avbps(average bitrate per second)
# are still unknown how to derive them, at least video wise it is possible, no idea how it's calculated audio wise nor anything else
# seems like it could be random values and the USM would still work.
class USMBuilder:
    pass