from io import FileIO, BytesIO
from typing import BinaryIO, Generator
from struct import Struct

IvfChunkHeaderStruct = Struct("<4sHH4sHHIIII")
IvfFrameChunkHeaderStruct = Struct("<IQ")

class IVF:
    """ IVF class for VP9 containers which are used on USM's """
    __slots__ = ["ivf", "stream"]
    ivf: dict
    stream: BinaryIO
    
    def __init__(self, ivffile) -> None:
        """ Loads in the IVF file. """
        if type(ivffile) == str:
            self.stream = FileIO(ivffile)
        else:
            self.stream = BytesIO(ivffile)
        self.loadfile()
    
    def loadfile(self) -> None:
        """ Cache data into class property. """
        header, version, header_len, codec, width, height, tbd, tbn, num_frames, reserved = IvfChunkHeaderStruct.unpack(
            self.stream.read(IvfChunkHeaderStruct.size)
        )

        if header != b'DKIF' and codec != b"VP90": # Only VP9.
            raise ValueError("Invalid or unsupported IVF file/codec.")

        self.ivf = dict(
            Header = header,
            Version = version,
            HeaderSize = header_len,
            Codec = codec,
            Width = width,
            Height = height,
            time_base_denominator = tbd,
            time_base_numerator = tbn,
            FrameCount = num_frames,
            Reserved = reserved
        )
        self.stream.seek(header_len, 0)
    
    def get_frames(self) -> Generator:
        """ Generator function to retrieve Frame size, Frame time, Frame number, and Frame data. """
        for i in range(self.ivf['FrameCount']):
            FrameSize, TimeStamp = IvfFrameChunkHeaderStruct.unpack(
                self.stream.read(IvfFrameChunkHeaderStruct.size)
            )
            yield (FrameSize, TimeStamp, i, self.stream.read(FrameSize)) # Basically, (len, time, framenum, data)
    
    def info(self) -> dict:
        return self.ivf