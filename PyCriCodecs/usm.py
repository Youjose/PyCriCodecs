import os
from typing import BinaryIO
from io import FileIO, BytesIO
from .chunk import *
from .utf import UTF, UTFBuilder
from .ivf import IVF
from .adx import ADX
from .hca import HCA

# Big thanks and credit for k0lb3 and 9th helping me write this specific code.
# Also credit for the original C++ code from Nyagamon/bnnm.

# Apparently there is an older USM format called SofDec? This is for SofDec2 though.
# Extraction working only for now, although check https://github.com/donmai-me/WannaCRI/
# code for a complete breakdown of the USM format.
class USM:
    """ USM class for extracting infromation and data from a USM file. """
    __slots__ = ["filename", "videomask1", "videomask2", "audiomask", "decrypt", 
                "stream", "__fileinfo", "CRIDObj", "size", "output", "codec", "demuxed"]
    filename: BinaryIO
    videomask1: bytearray
    videomask2: bytearray
    audiomask: bytearray
    decrypt: bool
    stream: BinaryIO
    __fileinfo: list
    CRIDObj: UTF
    output: dict
    size: int
    codec: int
    demuxed: bool

    def __init__(self, filename, key: str = False):
        """
        Sets the decryption status, if the key is not given, it will return the plain SFV data.
        If the key is given the code will decrypt SFA data if it was ADX, otherwise return plain SFA data.
        """
        self.filename = filename
        self.decrypt = False

        
        if key and type(key) != bool:
            self.decrypt = True
            self.init_key(key)
        self.load_file()
    
    def init_key(self, key: str):
        if type(key) == str:
            if len(key) <= 16:
                key = key.rjust(16, "0")
                key1 = bytes.fromhex(key[8:])
                key2 = bytes.fromhex(key[:8])
            else:
                raise ValueError("Invalid input key.")
        elif type(key) == int:
            key1 = int.to_bytes(key & 0xFFFFFFFF, 4, "big")
            key2 = int.to_bytes(key >> 32, 4, "big")
        else:
            raise ValueError("Invalid key format, must be either a string or an integer.")
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
            raise NotImplementedError(f"Unsupported file type: {header}")
        self.stream.seek(0)
        self.demuxed = False
    
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
        output = dict()
        for i in range(len(headers)):
            output[headers[i]+"_"+str(chnos[i])] = bytearray()
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
                    output[header.decode()+"_"+str(chno)].extend(data)
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
                        output[header.decode()+"_0"] = bytearray()
                        data = self.reader(chuncksize, offset, padding, header)
                        output[header.decode()+"_0"].extend(data) # No channel number info, code here assumes it's a one channel data type.
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
        self.demuxed = True

    def extract(self, dirname: str = ""):
        """ Extracts all USM contents. """
        self.stream.seek(0)
        if not self.demuxed:
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
        for chunk, data in self.output.items():
            chunk = chunk.rsplit("_", 1)[0]
            if dirname or "\\" in filenames[point] or "/" in filenames[point] or os.sep in filenames[point]:
                os.makedirs(os.path.dirname(filenames[point]), exist_ok=True)
            if chunk == USMChunckHeaderType.SBT.value.decode():
                # Subtitle information.
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
            elif chunk == USMChunckHeaderType.CUE.value.decode():
                # CUE chunks is actually just metadata.
                # and can be accessed by get_metadata() function after demuxing or extracting.
                point += 1
            elif data == bytearray():
                # This means it has no data, and just like the CUE, it might be just metadata. 
                point += 1
            elif filenames[point] == "":
                # Rare case and might never happen unless the USM is artificially edited. 
                fl = table[0]["filename"][1].rsplit(".", 1)[0] + "_" + str(point) + ".bin"
                open(fl, "wb").write(data)
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
        # After searching, I found how the SBT format is actually made.
        # But the use case for them is not ideal as they are proprietary.
        # So I will just convert them to SRT.
        size = len(stream)
        stream: BytesIO = BytesIO(stream)
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
        """ Function to return USM metadata after demuxing. """
        return self.__fileinfo

# There are a lot of unknowns, minbuf(minimum buffer of what?) and avbps(average bitrate per second)
# are still unknown how to derive them, at least video wise it is possible, no idea how it's calculated audio wise nor anything else
# seems like it could be random values and the USM would still work.
class USMBuilder:
    __slots__ = ["ivfObj", "videomask1", "videomask2", "audiomask", "encrypt", "audio_codec",
                "streams", "encryptAudio", "SFA_chunk_size", "base_interval_per_SFA_chunk", 
                "video_codec", "SFV_interval_for_VP9", "audio", "video_filename", "minchk",
                "audio_filenames", "minbuf", "avbps", "key", "usm"]
    ivfObj: IVF
    videomask1: bytearray
    videomask2: bytearray
    audiomask: bytearray
    encrypt: bool
    audio_codec: str
    streams: list
    encryptAudio: bool
    SFA_chunk_size: list
    base_interval_per_SFA_chunk: list
    video_codec: str
    SFV_interval_for_VP9: float
    audio: bool
    video_filename: str
    audio_filenames: list
    minchk: int
    minbuf: int
    avbps: int
    key: int
    usm: bytes

    def __init__(self, video, audio = False, key = False, audio_codec: str = "adx", encryptAudio: bool = False) -> None:
        """ USM constructor, needs a video to build a USM. """
        if type(video) == str:
            videostream = FileIO(video)
            self.video_filename = video
        else:
            videostream = BytesIO(video)
            self.video_filename = "temp.ivf"
        
        header = videostream.read(4)

        if header == USMChunckHeaderType.CRID.value:
            raise NotImplementedError("USM editing is not implemented yet.")
            # self.load_usm()
            # self.ivfObj = False
            # self.encryptAudio = encryptAudio
            # self.audio_codec = audio_codec.lower()
            # self.encrypt = False
            # if key:
            #     self.init_key(key)
            #     self.encrypt = True
        elif header != VideoType.IVF.value:
            raise NotImplementedError("Video container must be in IVF format containing VP9 codec.")
        else:
            videostream.seek(0)
            self.ivfObj = IVF(videostream)
            self.video_codec = "vp9"
            self.audio_codec = audio_codec.lower()
            self.encrypt = False
            self.audio = False
            self.encryptAudio = encryptAudio
            self.key = 0
            if encryptAudio and not key:
                raise ValueError("Cannot encrypt Audio without key.")
            if key:
                self.init_key(key)
                self.encrypt = True
            if audio:
                self.load_audio(audio)
                self.audio = True
    
    def load_audio(self, audio):
        self.audio_filenames = []
        if type(audio) == list:
            count = 0
            for track in audio:
                if type(track) == str:
                    self.audio_filenames.append(track)
                else:
                    self.audio_filenames.append("{:02d}.sfa".format(count))
                    count += 1
        else:
            if type(audio) == str:
                self.audio_filenames.append(audio)
            else:
                self.audio_filenames.append("00.sfa")

        self.streams = []
        if self.audio_codec == "adx":
            if type(audio) == list:
                for track in audio:
                    adxObj = ADX(track)
                    if adxObj.filetype == "wav":
                        adxObj.encode(AdxVersion=4, Encoding=3, force_not_looping=True)
                    self.streams.append(adxObj)
            else:
                adxObj = ADX(audio)
                if adxObj.filetype == "wav":
                    adxObj.encode(AdxVersion=4, Encoding=3, force_not_looping=True)
                self.streams.append(adxObj)
        elif self.audio_codec == "hca":
            if type(audio) == list:
                for track in audio:
                    hcaObj = HCA(track, key=self.key)
                    if hcaObj.filetype == "wav":
                        hcaObj.encode(force_not_looping=True, encrypt=self.encryptAudio, keyless=False)
                    self.streams.append(hcaObj)
            else:
                hcaObj = HCA(audio, key=self.key)
                if hcaObj.filetype == "wav":
                    hcaObj.encode(force_not_looping=True, encrypt=self.encryptAudio, keyless=False)
                self.streams.append(hcaObj)
        else:
            raise ValueError("Supported audio codecs in USM are only HCA and ADX.")
    
    def append_stream(self, audio):
        assert type(audio) != list
        if self.audio_codec == "adx":
            adxObj = ADX(audio)
            if adxObj.filetype == "wav":
                adxObj.encode(AdxVersion=4, Encoding=3, force_not_looping=True)
            self.streams.append(adxObj)
        elif self.audio_codec == "hca":
            hcaObj = HCA(audio, self.key)
            if hcaObj.filetype == "wav":
                hcaObj.encode(force_not_looping=True, encrypt=self.encryptAudio, keyless=False)
            self.streams.append(hcaObj)
        else:
            raise ValueError("Supported audio codecs in USM are only HCA and ADX.")
    
    def build(self) -> bytes:
        if not self.ivfObj:
            raise NotImplementedError("Loaded USM is  not supported yet.") # saved with get_usm()
        if self.audio:
            self.prepare_SFA()
        self.prepare_SFV()
        # This will be a hit to performance, but I will store the building USM on memory instead of
        # flushing it to disk right away, this in case something going wrong.
        self.get_data()
    
    # So, so bad. FIXME
    def get_data(self) -> bytes:
        ivfinfo = self.ivfObj.info()
        self.ivfObj.stream.seek(0)
        current_interval = 0
        v_framerate = int((ivfinfo["time_base_denominator"] / ivfinfo["time_base_numerator"]) * 100)
        SFV_header = self.ivfObj.stream.read(ivfinfo["HeaderSize"])

        #########################################
        # SFV chunks generator.
        #########################################
        SFV_list = []
        SFV_chunk = b''
        count = 0
        self.minchk = 0
        self.minbuf = 0
        bitrate = 0
        for data in self.ivfObj.get_frames():
            # SFV has priority in chunks, it comes first.
            pad_len = data[0] + len(SFV_header) if count == 0 else data[0]
            padding = (0x20 - (pad_len % 0x20) if pad_len % 0x20 != 0 else 0)
            SFV_chunk = USMChunkHeader.pack(
                                        USMChunckHeaderType.SFV.value,
                                        pad_len + 0x18 + padding,
                                        0,
                                        0x18,
                                        padding,
                                        0,
                                        0,
                                        0,
                                        0,
                                        current_interval,
                                        v_framerate,
                                        0,
                                        0
                                        )
            temp = data[3]
            if count == 0:
                temp = SFV_header + temp
            if self.encrypt:
                temp = self.VideoMask(temp)
            SFV_chunk += temp
            SFV_chunk = SFV_chunk.ljust(pad_len + 0x18 + padding + 0x8, b"\x00")
            SFV_list.append(SFV_chunk)
            count += 1
            current_interval = int(count * self.SFV_interval_for_VP9)
            if data[4]:
                self.minchk += 1
            if self.minbuf < pad_len:
                self.minbuf = pad_len
            bitrate += (pad_len * 8 * (v_framerate/100))
        else:
            self.avbps = int(bitrate/count)
            SFV_chunk = USMChunkHeader.pack(
                        USMChunckHeaderType.SFV.value,
                        0x38,
                        0,
                        0x18,
                        0,
                        0,
                        0,
                        0,
                        2,
                        0,
                        30,
                        0,
                        0
                        )
            SFV_chunk += b"#CONTENTS END   ===============\x00"
            SFV_list.append(SFV_chunk)
        #########################################
        # SFV chunks generator end.
        #########################################

        #########################################
        # SFA chunks generator.
        #########################################
        if self.audio:
            SFA_chunks = [[] for i in range(len(self.streams))]
            for stream in self.streams:
                current_interval = 0
                if self.audio_codec == "adx":
                    stream.sfaStream.seek(0, 2)
                    stream_size = stream.sfaStream.tell() - (0x12 if stream.filetype == "wav" else stream.Blocksize)
                    stream.sfaStream.seek(0)
                    count = 0
                    while stream.sfaStream.tell() < stream_size:
                        if stream.sfaStream.tell() == 0:
                            if stream.filetype == "wav":
                                do = 0x120
                            else:
                                do = stream.dataOffset+4
                        else:
                            # Compute expensive.
                            do = (stream_size - (0x120 if stream.filetype == "wav" else stream.dataOffset+4) - self.SFA_chunk_size[self.streams.index(stream)]) % self.SFA_chunk_size[self.streams.index(stream)] if stream.sfaStream.tell() + self.SFA_chunk_size[self.streams.index(stream)] > stream_size else self.SFA_chunk_size[self.streams.index(stream)]
                        padding = (0x20 - (do % 0x20) if do % 0x20 != 0 else 0)
                        SFA_chunk = USMChunkHeader.pack(
                                USMChunckHeaderType.SFA.value,
                                do + 0x18 + padding,
                                0,
                                0x18,
                                padding,
                                self.streams.index(stream),
                                0,
                                0,
                                0,
                                current_interval,
                                2997,
                                0,
                                0
                                )
                        temp_stream = stream.sfaStream.read(do)
                        if self.encryptAudio:
                            temp_stream = self.AudioMask(temp_stream)
                        SFA_chunk += temp_stream.ljust(do + padding, b"\x00")
                        SFA_chunks[self.streams.index(stream)].append(SFA_chunk)
                        current_interval = int(count * self.base_interval_per_SFA_chunk[self.streams.index(stream)])
                        count += 1
                    else:
                        do = (0x12 if stream.filetype == "wav" else stream.Blocksize)
                        padding = (0x20 - (do % 0x20) if do % 0x20 != 0 else 0)
                        SFA_chunk = USMChunkHeader.pack(
                                USMChunckHeaderType.SFA.value,
                                do + 0x18 + padding,
                                0,
                                0x18,
                                padding,
                                self.streams.index(stream),
                                0,
                                0,
                                0,
                                current_interval,
                                2997,
                                0,
                                0
                                )
                        SFA_chunk += stream.sfaStream.read(do).ljust(do + padding, b"\x00")
                        SFA_chunks[self.streams.index(stream)].append(SFA_chunk)
                        current_interval = int(count * self.base_interval_per_SFA_chunk[self.streams.index(stream)])
                        SFA_chunk = USMChunkHeader.pack(
                                    USMChunckHeaderType.SFA.value,
                                    0x38,
                                    0,
                                    0x18,
                                    0,
                                    self.streams.index(stream),
                                    0,
                                    0,
                                    2,
                                    0,
                                    30,
                                    0,
                                    0
                                    )
                        SFA_chunk += b"#CONTENTS END   ===============\x00"
                        SFA_chunks[self.streams.index(stream)][-1]+=SFA_chunk
                else:
                    stream: HCA
                    padding = (0x20 - (stream.hca["HeaderSize"] % 0x20) if stream.hca["HeaderSize"] % 0x20 != 0 else 0)
                    SFA_chunk = USMChunkHeader.pack(
                            USMChunckHeaderType.SFA.value,
                            stream.hca["HeaderSize"] + 0x18 + padding,
                            0,
                            0x18,
                            padding,
                            self.streams.index(stream),
                            0,
                            0,
                            0,
                            current_interval,
                            2997,
                            0,
                            0
                            )
                    SFA_chunk += stream.get_header().ljust(stream.hca["HeaderSize"]+ padding, b"\x00")
                    SFA_chunks[self.streams.index(stream)].append(SFA_chunk)
                    for i in stream.get_frames():
                        padding = (0x20 - (stream.hca["FrameSize"] % 0x20) if stream.hca["FrameSize"] % 0x20 != 0 else 0)
                        SFA_chunk = USMChunkHeader.pack(
                                USMChunckHeaderType.SFA.value,
                                stream.hca["FrameSize"] + 0x18 + padding,
                                0,
                                0x18,
                                padding,
                                self.streams.index(stream),
                                0,
                                0,
                                0,
                                current_interval,
                                2997,
                                0,
                                0
                                )
                        SFA_chunk += i[1].ljust(stream.hca["FrameSize"] + padding , b"\x00")
                        current_interval += self.base_interval_per_SFA_chunk[self.streams.index(stream)]
                        SFA_chunks[self.streams.index(stream)].append(SFA_chunk)
                    else:
                        SFA_chunk = USMChunkHeader.pack(
                                    USMChunckHeaderType.SFA.value,
                                    0x38,
                                    0,
                                    0x18,
                                    0,
                                    self.streams.index(stream),
                                    0,
                                    0,
                                    2,
                                    0,
                                    30,
                                    0,
                                    0
                                    )
                        SFA_chunk += b"#CONTENTS END   ===============\x00"
                        SFA_chunks[self.streams.index(stream)][-1]+=SFA_chunk
        #########################################
        # SFA chunks generator end.
        #########################################
        if self.audio:
            self.build_usm(SFV_list, SFA_chunks)
        else:
            self.streams = []
            self.build_usm(SFV_list)
        
    
    # TODO Add support for Subtitle information.
    def build_usm(self, SFV_list: list, SFA_chunks: list = False, SBT_chunks = None):
        header = self.build_header(SFV_list, SFA_chunks, SBT_chunks)
        len_sfv = len(SFV_list)
        if self.audio:
            len_sfa = [len(x) for x in SFA_chunks]
        else:
            len_sfa = [0]
        max_len = max(len_sfv, max(len_sfa))

        # SFV gets the order priority if the interval is matching that of SFA
        # furthermore, SFA chunks keep going until the next SFV interval is reached.
        # 
        current_interval = 0
        target_interval = 0
        sfa_count = 0
        for i in range(max_len):
            if i < len_sfv:
                header += SFV_list[i]
            target_interval += self.SFV_interval_for_VP9

            if self.audio:
                while current_interval < target_interval:
                    idx = 0
                    for stream in SFA_chunks:
                        if current_interval > target_interval:
                            # This would not just break the loop, this would break everything.
                            # Will not happen in typical cases. But if a video had a really weird framerate, this might skew it.
                            current_interval += self.base_interval_per_SFA_chunk[0] # Not safe. FIXME
                            break 
                        if sfa_count == 0:
                            header += stream[sfa_count]
                        if sfa_count < len_sfa[idx]-1:
                            header += stream[sfa_count+1]
                        idx += 1
                    else:
                        current_interval += self.base_interval_per_SFA_chunk[0]
                        # This is wrong actually, I made the base interval a list in case the intervals are different
                        # But it seems they are the same no matter what, however I will leave it as this just in case.
                        sfa_count += 1
        self.usm = header
    
    def build_header(self, SFV_list: list, SFA_chunks: list = False, SBT_chunks = None) -> bytes:

        CRIUSF_DIR_STREAM = [
            dict(
                avbps = (UTFTypeValues.uint, -1), # Will be updated later.
                chno = (UTFTypeValues.ushort, 0xFFFF),
                datasize = (UTFTypeValues.uint, 0),
                filename = (UTFTypeValues.string, self.video_filename.rsplit(".", 1)[0]+".usm"),
                filesize = (UTFTypeValues.uint, -1), # Will be updated later.
                fmtver = (UTFTypeValues.uint, 16777984),
                minbuf = (UTFTypeValues.uint, -1), # Will be updated later.
                minchk = (UTFTypeValues.ushort, 1),
                stmid = (UTFTypeValues.uint, 0)
            )
        ]

        total_avbps = self.avbps
        minbuf = 4 + self.minbuf

        self.ivfObj.stream.seek(0, 2)
        v_filesize = self.ivfObj.stream.tell()
        self.ivfObj.stream.seek(0)

        video_dict = dict(
            avbps = (UTFTypeValues.uint, self.avbps),
            chno = (UTFTypeValues.ushort, 0),
            datasize = (UTFTypeValues.uint, 0),
            filename = (UTFTypeValues.string, self.video_filename),
            filesize = (UTFTypeValues.uint, v_filesize),
            fmtver = (UTFTypeValues.uint, 16777984),
            minbuf = (UTFTypeValues.uint, self.minbuf),
            minchk = (UTFTypeValues.ushort, self.minchk),
            stmid = (UTFTypeValues.uint, int.from_bytes(USMChunckHeaderType.SFV.value, "big"))
        )
        CRIUSF_DIR_STREAM.append(video_dict)

        if self.audio:
            chno = 0
            for stream in self.streams:
                sz = 0
                if self.audio_codec == "adx":
                    stream: ADX
                    stream.sfaStream.seek(0, 2)
                    sz = stream.sfaStream.tell()
                    stream.sfaStream.seek(0)
                    if stream.filetype == "wav":
                        chnls = stream.fmtChannelCount
                    else:
                        chnls = stream.channelCount
                    # I am not sure if this only works when there's one audio stream. TODO
                    avbps = (sz * 8 * chnls) - sz
                else:
                    stream: HCA
                    stream.hcastream.seek(0, 2)
                    sz = stream.hcastream.tell()
                    stream.hcastream.seek(0)
                    if stream.filetype == "wav":
                        chnls = stream.fmtChannelCount
                    else:
                        chnls = stream.hca['ChannelCount']
                    # I don't know how this is derived so I am putting my best guess here. TODO
                    avbps = int(sz / chnls)
                total_avbps += avbps
                minbuf += 27860
                audio_dict = dict(
                    avbps = (UTFTypeValues.uint, avbps),
                    chno = (UTFTypeValues.ushort, chno),
                    datasize = (UTFTypeValues.uint, 0),
                    filename = (UTFTypeValues.string, self.audio_filenames[chno]),
                    filesize = (UTFTypeValues.uint, sz),
                    fmtver = (UTFTypeValues.uint, 16777984),
                    minbuf = (UTFTypeValues.uint, 27860), # minbuf is fixed at that for audio.
                    minchk = (UTFTypeValues.ushort, 1),
                    stmid = (UTFTypeValues.uint, int.from_bytes(USMChunckHeaderType.SFA.value, "big"))
                )
                CRIUSF_DIR_STREAM.append(audio_dict)
                chno += 1

        CRIUSF_DIR_STREAM[0]["avbps"]  = (UTFTypeValues.uint, total_avbps)
        CRIUSF_DIR_STREAM[0]["minbuf"] = (UTFTypeValues.uint, minbuf) # Wrong. TODO Despite being fixed per SFA stream, seems to change internally before summation.

        v_framrate = int(round(self.ivfObj.ivf['time_base_denominator'] / self.ivfObj.ivf['time_base_numerator'], 3) * 1000)
        VIDEO_HDRINFO = [
            {
                'alpha_type': (UTFTypeValues.uint, 0),
                'color_space': (UTFTypeValues.uint, 0),
                'disp_height': (UTFTypeValues.uint, self.ivfObj.ivf["Height"]),
                'disp_width': (UTFTypeValues.uint, self.ivfObj.ivf["Width"]),
                'framerate_d': (UTFTypeValues.uint, 1000),
                'framerate_n': (UTFTypeValues.uint, v_framrate),
                'height': (UTFTypeValues.uint, self.ivfObj.ivf["Height"]),
                'ixsize': (UTFTypeValues.uint, self.minbuf),
                'mat_height': (UTFTypeValues.uint, self.ivfObj.ivf["Height"]),
                'mat_width': (UTFTypeValues.uint, self.ivfObj.ivf["Width"]),
                'max_picture_size': (UTFTypeValues.uint, 0),
                'metadata_count': (UTFTypeValues.uint, 1), # Could be 0 and ignore metadata?
                'metadata_size': (UTFTypeValues.uint, 224), # Not the actual value, I am just putting default value for one seek info.
                'mpeg_codec': (UTFTypeValues.uchar, 9),
                'mpeg_dcprec': (UTFTypeValues.uchar, 0),
                'picture_type': (UTFTypeValues.uint, 0),
                'pre_padding': (UTFTypeValues.uint, 0),
                'scrn_width': (UTFTypeValues.uint, 0),
                'total_frames': (UTFTypeValues.uint, self.ivfObj.ivf["FrameCount"]),
                'width': (UTFTypeValues.uint, self.ivfObj.ivf["Width"])
            }
        ]
        v = UTFBuilder(VIDEO_HDRINFO, table_name="VIDEO_HDRINFO")
        v.strings = b"<NULL>\x00" + v.strings
        VIDEO_HDRINFO = v.parse()
        padding = (0x20 - (len(VIDEO_HDRINFO) % 0x20) if (len(VIDEO_HDRINFO) % 0x20) != 0 else 0)
        chk = USMChunkHeader.pack(
            USMChunckHeaderType.SFV.value,
            len(VIDEO_HDRINFO) + 0x18 + padding,
            0,
            0x18,
            padding,
            0,
            0,
            0,
            1,
            0,
            30,
            0,
            0
        )
        chk += VIDEO_HDRINFO.ljust(len(VIDEO_HDRINFO) + padding, b"\x00")
        VIDEO_HDRINFO = chk

        audio_metadata = []
        if self.audio:
            if self.audio_codec == "hca":
                chno = 0
                for stream in self.streams:
                    payload = [
                            dict(
                                hca_header = (UTFTypeValues.bytes, stream.get_header())
                            )
                        ]
                    p = UTFBuilder(payload, table_name="AUDIO_HEADER")
                    p.strings = b"<NULL>\x00" + p.strings
                    metadata = p.parse()
                    padding = 0x20 - (len(metadata) % 0x20) if len(metadata) % 0x20 != 0 else 0
                    chk = USMChunkHeader.pack(
                        USMChunckHeaderType.SFA.value,
                        len(metadata) + 0x18 + padding,
                        0, 
                        0x18,
                        padding,
                        chno,
                        0,
                        0,
                        3,
                        0,
                        30,
                        0,
                        0
                    )
                    chk += metadata
                    chk.ljust(len(metadata) + padding, b"\x00")
                    audio_metadata.append(chk)
                    chno += 1


            audio_headers = []
            chno = 0
            for stream in self.streams:
                if self.audio_codec == "adx":
                    if stream.filetype == "wav":
                        chnls = stream.fmtChannelCount
                        sampling_rate = stream.fmtSamplingRate
                        total_samples = int(stream.dataSize // stream.fmtSamplingSize)
                    else:
                        chnls = stream.channelCount
                        sampling_rate = stream.SamplingRate
                        total_samples = stream.SampleCount
                else:
                    chnls = stream.hca["ChannelCount"]
                    sampling_rate = stream.hca["SampleRate"]
                    total_samples = stream.hca['FrameCount']
                AUDIO_HDRINFO = [
                    {
                        "audio_codec": (UTFTypeValues.uchar, (2 if self.audio_codec == "adx" else 4)),
                        "ixsize": (UTFTypeValues.uint, 27860),
                        "metadata_count": (UTFTypeValues.uint, (0 if self.audio_codec == "adx" else 1)),
                        "metadat_size": (UTFTypeValues.uint, (0 if self.audio_codec == "adx" else len(audio_metadata[chno]))),
                        "num_channels": (UTFTypeValues.uchar, chnls),
                        "sampling_rate": (UTFTypeValues.uint, sampling_rate),
                        "total_samples": (UTFTypeValues.uint, total_samples)
                    }
                ]
                if self.audio_codec == "hca":
                    AUDIO_HDRINFO[0].update({"ambisonics": (UTFTypeValues.uint, 0)})
                p = UTFBuilder(AUDIO_HDRINFO, table_name="AUDIO_HDRINFO")
                p.strings = b"<NULL>\x00" + p.strings
                header = p.parse()
                padding = (0x20 - (len(header) % 0x20) if (len(header) % 0x20) != 0 else 0)
                chk = USMChunkHeader.pack(
                    USMChunckHeaderType.SFA.value,
                    len(header) + 0x18 + padding,
                    0,
                    0x18,
                    padding,
                    chno,
                    0,
                    0,
                    1,
                    0,
                    30,
                    0,
                    0
                )
                chk += header.ljust(len(header) + padding, b"\x00")
                audio_headers.append(chk)
                chno += 1

        first_chk_ofs = 0x800 + len(VIDEO_HDRINFO) + 0x20 + 0x40 * len(self.streams) + 192 + (0 if not self.audio else sum([len(x) + 0x40 for x in audio_headers]) + (sum([len(x) + 0x40 for x in audio_metadata]) if self.audio_codec == "hca" else 0))
        VIDEO_SEEKINFO = [
            {
                'num_skip': (UTFTypeValues.short, 0),
                'ofs_byte': (UTFTypeValues.ullong, first_chk_ofs),
                'ofs_frmid': (UTFTypeValues.int, 0),
                'resv': (UTFTypeValues.short, 0)
            }
        ]

        total_len = sum([len(x) for x in SFV_list]) + first_chk_ofs
        if self.audio:
            sum_len = 0
            for stream in SFA_chunks:
                for x in stream:
                    sum_len += len(x)
            total_len += sum_len
        
        CRIUSF_DIR_STREAM[0]["filesize"] = (UTFTypeValues.uint, total_len)
        CRIUSF_DIR_STREAM = UTFBuilder(CRIUSF_DIR_STREAM, table_name="CRIUSF_DIR_STREAM")
        CRIUSF_DIR_STREAM.strings = b"<NULL>\x00" + CRIUSF_DIR_STREAM.strings
        CRIUSF_DIR_STREAM = CRIUSF_DIR_STREAM.parse()

        ##############################################
        # Parsing everything.
        ##############################################
        header = bytes()
        # CRID
        padding = 0x800 - len(CRIUSF_DIR_STREAM)
        CRID = USMChunkHeader.pack(
            USMChunckHeaderType.CRID.value,
            0x800 - 0x8,
            0,
            0x18,
            padding - 0x20,
            0,
            0,
            0,
            1,
            0,
            30,
            0,
            0
        )
        CRID += CRIUSF_DIR_STREAM.ljust(0x800-0x20, b"\x00")
        header += CRID

        # Header chunks
        header += VIDEO_HDRINFO
        if self.audio:
            SFA_END = []
            count = 0
            for chunk in audio_headers:
                header += chunk
                SFA_chk_END = USMChunkHeader.pack(
                    USMChunckHeaderType.SFA.value,
                    0x38,
                    0,
                    0x18,
                    0x0,
                    count,
                    0x0,
                    0x0,
                    2,
                    0,
                    30,
                    0,
                    0
                )
                SFA_END.append(SFA_chk_END + b"#HEADER END     ===============\x00")
                count += 1
        SFV_END = USMChunkHeader.pack(
            USMChunckHeaderType.SFV.value,
            0x38,
            0,
            0x18,
            0x0,
            0x0,
            0x0,
            0x0,
            2,
            0,
            30,
            0,
            0
        )
        SFV_END += b"#HEADER END     ===============\x00"

        header += SFV_END
        if self.audio:
            for chk in SFA_END:
                header += chk
        
        VIDEO_SEEKINFO = UTFBuilder(VIDEO_SEEKINFO, table_name="VIDEO_SEEKINFO")
        VIDEO_SEEKINFO.strings = b"<NULL>\x00" + VIDEO_SEEKINFO.strings
        VIDEO_SEEKINFO = VIDEO_SEEKINFO.parse()
        padding = 0x20 - len(VIDEO_SEEKINFO) % 0x20 if len(VIDEO_SEEKINFO) % 0x20 != 0 else 0
        seekinf = USMChunkHeader.pack(
            USMChunckHeaderType.SFV.value,
            len(VIDEO_SEEKINFO) + 0x18 + padding,
            0,
            0x18,
            padding,
            0,
            0,
            0,
            3,
            0,
            30,
            0,
            0
        )
        seekinf += VIDEO_SEEKINFO.ljust(len(VIDEO_SEEKINFO) + padding, b"\x00")
        header += seekinf

        if self.audio and self.audio_codec == "hca":
            count = 0
            metadata_end = []
            for metadata in audio_metadata:
                header += metadata
                SFA_chk_END = USMChunkHeader.pack(
                    USMChunckHeaderType.SFA.value,
                    0x38,
                    0,
                    0x18,
                    0x0,
                    count,
                    0x0,
                    0x0,
                    2,
                    0,
                    30,
                    0,
                    0
                )
                metadata_end.append(SFA_chk_END + b"#METADATA END   ===============\x00")
                count += 1
        SFV_END = USMChunkHeader.pack(
            USMChunckHeaderType.SFV.value,
            0x38,
            0,
            0x18,
            0x0,
            0x0,
            0x0,
            0x0,
            2,
            0,
            30,
            0,
            0
        )
        SFV_END += b"#METADATA END   ===============\x00"
        header += SFV_END

        if self.audio and self.audio_codec == "hca":
            for chk in metadata_end:
                header += chk
        
        return header
        
    def prepare_SFV(self):
        if self.video_codec == "vp9":
            ivfinfo = self.ivfObj.info()
            v_framerate = round(ivfinfo["time_base_denominator"] / ivfinfo["time_base_numerator"], 2)
            framerate = 2997
            self.SFV_interval_for_VP9 = round(framerate / v_framerate, 1) # Not the actual interval for the VP9 codec, but USM calculate this way.
    
    def prepare_SFA(self):
        """ Generates info needed per SFA stream. """
        self.SFA_chunk_size = []
        self.base_interval_per_SFA_chunk = []
        framerate = 29.97
        # I just noticed that audio framerate is not based off the video input, in fact, it seems to be locked at 29.97
        if self.audio_codec == "adx":
            # The Audio chunks must be equal in size? or could it vary per audio stream?
            # This is weird since chunksize is dictated by the sampling rate and channel count. (blocksize as well)
            for adx in self.streams:
                adx: ADX
                if adx.filetype == "wav":
                    self.SFA_chunk_size.append(int(adx.fmtSamplingRate // framerate // 32) * (18 * adx.fmtChannelCount)) # 18 is standard ADX blocksize.
                else:
                    self.SFA_chunk_size.append(int(adx.SamplingRate // framerate // 32) * (adx.Blocksize * adx.channelCount))
                self.base_interval_per_SFA_chunk.append(99.9 if self.video_codec == "vp9" else 100) # For VP9, this is the only repeating pattern I found. Anything else has another interval.
        else:
            # HCA chunks here, which are harder a bit, since the interval per chunks is rather derived
            # from the resulting framesize of the HCA as well sample rate.
            # However, SFA chunk size is at least given.
            for hca in self.streams:
                hca: HCA
                hca.Pyparse_header()
                framesize = hca.hca["FrameSize"]
                self.SFA_chunk_size.append(framesize)
                self.base_interval_per_SFA_chunk.append(64) # I am not sure about this.
    
    def init_key(self, key: str):
        # Copied from USM class, it's hard to combine them at this point with how the USM class is created for extraction.
        if type(key) == str:
            if len(key) < 16:
                key = key.rjust(16, "0")
                self.key == int(key, 16)
                key1 = bytes.fromhex(key[8:])
                key2 = bytes.fromhex(key[:8])
            else:
                raise ValueError("Inavild input key.")
        elif type(key) == int:
            self.key = key
            key1 = int.to_bytes(key & 0xFFFFFFFF, 4, "big")
            key2 = int.to_bytes(key >> 32, 4, "big")
        else:
            raise ValueError("Invalid key format, must be either a string or an integer.")
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

    # Decrypt SFV chunks or ALP chunks, should only be used if the video data is encrypted.
    def VideoMask(self, memObj: bytes) -> bytes:
        head = memObj[:0x40]
        memObj = bytearray(memObj[0x40:])
        size = len(memObj)
        # memObj len is a cached property, very fast to lookup
        if size <= 0x200:
            return (head + memObj)
        data_view = memoryview(memObj)


        # mask 1
        mask = bytearray(self.videomask1)
        mask_view = memoryview(mask)
        mask_index = 0
        for i in range(0x100):
            mask_view[mask_index] ^= data_view[i + 0x100]
            data_view[i] ^= mask_view[mask_index]
            mask_index = (mask_index + 1) % 32

        # mask 2
        mask = bytearray(self.videomask2)
        mask_view = memoryview(mask)
        vmask = self.videomask2
        vmask_view = memoryview(vmask)

        mask_index = 0

        for i in range(0x100, size):
            temp = data_view[i]
            data_view[i] ^= mask_view[mask_index]
            mask_view[mask_index] = temp ^ vmask_view[mask_index]
            mask_index = (mask_index + 1) % 32

        return bytes(head + memObj)

    def AudioMask(self, memObj: bytes) -> bytes:
        head = memObj[:0x140]
        memObj = bytearray(memObj[0x140:])
        size = len(memObj)
        data_view = memoryview(memObj)
        mask = bytearray(self.audiomask)
        mask_view = memoryview(mask)
        for i in range(size):
            data_view[i] ^= mask_view[i%32]
        return bytes(head + memObj)
    
    def get_usm(self) -> bytes:
        return self.usm