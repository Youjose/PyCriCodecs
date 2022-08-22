from io import BytesIO, FileIO
import os
from typing import BinaryIO
from struct import iter_unpack, pack
from .chunk import *
from .hca import HCA

# for AFS2 only.
class AWB:
    """ Use this class to return any AWB data with the getfiles function. """
    __slots__ = ["stream", "numfiles", "align", "subkey", "version", "ids", "ofs", "filename", "headersize"]
    stream: BinaryIO
    numfiles: int
    align: int
    subkey: bytes
    version: int
    ids: list[int]
    ofs: list[int]
    filename: str
    headersize: int

    def __init__(self, stream) -> None:
        if type(stream) == str:
            self.stream = FileIO(stream)
            self.filename = stream
        else:
            self.stream = BytesIO(stream)
            self.filename = ""
        self.readheader()
    
    def readheader(self):
        # Reads header.
        magic, self.version, intsize, unk06, self.numfiles, self.align, self.subkey = AWBChunkHeader.unpack(
            self.stream.read(AWBChunkHeader.size)
        )
        if magic != b'AFS2':
            raise ValueError("Invalid AWB header.")
        
        # Reads data in the header.
        self.ids = list()
        self.ofs = list()
        for i in iter_unpack("<H", self.stream.read(2*self.numfiles)):
            self.ids.append(i[0])
        for i in iter_unpack("<"+self.stringtypes(intsize), self.stream.read(intsize*(self.numfiles+1))):
            self.ofs.append(i[0] if i[0] % self.align == 0 else (i[0] + (self.align - (i[0] % self.align))))
        
        # Seeks to files offset.
        headersize = 16 + (intsize*(self.numfiles+1)) + (2*self.numfiles)
        if headersize % self.align != 0:
            self.headersize = headersize + (self.align - (headersize % self.align))
        self.stream.seek(self.headersize, 0)

    def extract(self, decode=False, key=0):
        """ Extracts the files. """
        count = 0
        for i in self.getfiles():
            # Apparently AWB's can have many types of files, focusing on HCA's here though. So TODO.
            if self.filename:
                if i.startswith(HCAType.HCA.value) or i.startswith(HCAType.EHCA.value):
                    if decode:
                        filename = self.filename.rsplit(".", 1)[0] + "_" + str(count) + ".wav"
                    else:
                        filename = self.filename.rsplit(".", 1)[0] + "_" + str(count) + ".hca"
                else:
                    # Probably ADX.
                    filename = self.filename.rsplit(".", 1)[0] + "_" + str(count) + ".dat"
                    open(filename, "wb").write(i)
                    count += 1
                    continue
                open(filename, "wb").write(i if not decode else HCA(i, key=key, subkey=self.subkey).decode())
                count += 1
            else:
                if i.startswith(HCAType.HCA.value) or i.startswith(HCAType.EHCA.value):
                    if decode:
                        open(str(count)+".wav", "wb").write(HCA(i, key=key, subkey=self.subkey).decode())
                    else:
                        open(str(count)+".hca", "wb").write(i)
                else:
                    open(str(count)+".dat", "wb").write(i)
                count += 1

    def getfiles(self):
        """ Generator function to yield data from an AWB. """
        for i in range(1, len(self.ofs)):
            data = self.stream.read((self.ofs[i]-self.ofs[i-1]))
            self.stream.seek(self.ofs[i], 0)
            yield data

    def stringtypes(self, intsize: int) -> str:
        if intsize == 1:
            return "B" # Probably impossible.
        elif intsize == 2:
            return "H"
        elif intsize == 4:
            return "I"
        elif intsize == 8:
            return "Q"
        else:
            raise ValueError("Unknown int size.")

class AWBBuilder:
    """ Use this class to build any AWB of any kind given a directory with files. """
    __slots__ = []

    # Didn't think I would manage this in one function.
    def __init__(self, dirname: str, outfile: str, version: int = 2, align: int = 0x20, subkey: int = 0) -> None:
        size = 0
        if dirname == "":
            raise ValueError("Invalid directory.")
        elif outfile == "":
            raise ValueError("Invalid output file name.")
        elif version == 1 and subkey != 0:
            raise ValueError("Cannot have a subkey with AWB version of 1.")
        elif subkey != 0:
            raise NotImplementedError("Subkey encryption is not supported yet.")

        ofs = []
        numfiles = 0
        for r, d, f in os.walk(dirname):
            for file in f:
                sz = os.stat(os.path.join(r, file)).st_size
                if sz % align != 0: # Doesn't always needs to be this way?
                    sz = sz + (align - sz % align)
                ofs.append(size+sz)
                size += sz
                numfiles += 1
        
        if size > 0xFFFFFFFF:
            intsize = 8 # Unsigned long long.
            strtype = "<Q"
        else:
            intsize = 4 # Unsigned int, but could be a ushort, never saw it as one before though.
            strtype = "<I"
        
        header = AWBChunkHeader.pack(
            b'AFS2', version, intsize, 0x2, numfiles, align, subkey
        )

        for i in range(numfiles):
            header += pack("<H", i)
        
        headersize = len(header) + intsize * numfiles + intsize
        ofs = [(x+headersize) + (align - ((x+headersize) % align)) if (x+headersize) % align != 0 else (x+headersize) for x in ofs]
        ofs = [headersize] + ofs

        for i in ofs:
            header += pack(strtype, i)
        
        if headersize % align != 0:
            header = header.ljust(headersize + (align - (headersize % align)), b"\x00")
        out = open(outfile, "wb")
        out.write(header)
        for r, d, f in os.walk(dirname):
            for file in f:
                fl = open(os.path.join(r, file), "rb").read()
                if len(fl) % align != 0:
                    fl = fl.ljust(len(fl) + (align - (len(fl) % align)), b"\x00")
                out.write(fl)
        out.close()