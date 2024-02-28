from io import BytesIO, FileIO
import os
from typing import BinaryIO
from struct import iter_unpack, pack
from .chunk import *
from .hca import HCA

# for AFS2 only.
class AWB:
    """ Use this class to return any AWB data with the getfiles function. """
    __slots__ = ["stream", "numfiles", "align", "subkey", "version", "ids", "ofs", "filename", "headersize", "id_alignment"]
    stream: BinaryIO
    numfiles: int
    align: int
    subkey: bytes
    version: int
    ids: list
    ofs: list
    filename: str
    headersize: int
    id_alignment: int

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
        magic, self.version, offset_intsize, id_intsize, self.numfiles, self.align, self.subkey = AWBChunkHeader.unpack(
            self.stream.read(AWBChunkHeader.size)
        )
        if magic != b'AFS2':
            raise ValueError("Invalid AWB header.")
        
        # Reads data in the header.
        self.ids = list()
        self.ofs = list()
        for i in iter_unpack(f"<{self.stringtypes(id_intsize)}", self.stream.read(id_intsize*self.numfiles)):
            self.ids.append(i[0])
        for i in iter_unpack(f"<{self.stringtypes(offset_intsize)}", self.stream.read(offset_intsize*(self.numfiles+1))):
            self.ofs.append(i[0] if i[0] % self.align == 0 else (i[0] + (self.align - (i[0] % self.align))))
        
        # Seeks to files offset.
        self.headersize = 16 + (offset_intsize*(self.numfiles+1)) + (id_intsize*self.numfiles)
        if self.headersize % self.align != 0:
            self.headersize = self.headersize + (self.align - (self.headersize % self.align))
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
    
    def getfile_atindex(self, index):
        """ Gets you a file at specific index. """
        index += 1
        self.stream.seek(self.ofs[index], 0)
        data = self.stream.read(self.ofs[index]-self.ofs[index-1])
        self.stream.seek(self.headersize, 0) # Seeks back to headersize for getfiles.
        return data

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
    __slots__ = ["dirname", "outfile", "version", "align", "subkey", "id_intsize"]

    def __init__(self, dirname: list[str], subkey: int = 0, version: int = 2, id_intsize = 0x2, align: int = 0x20) -> None:
        if dirname == "":
            raise ValueError("Invalid directory.")
        elif version == 1 and subkey != 0:
            raise ValueError("Cannot have a subkey with AWB version of 1.")
        elif id_intsize not in [0x2, 0x4, 0x8]:
            raise ValueError("id_intsize must be either 2, 4 or 8.")
        self.dirname = dirname
        self.version = version
        self.align = align
        self.subkey = subkey
        self.id_intsize = id_intsize

    def build(self, outfile):
        if outfile == "":
            raise ValueError("Invalid output file name.")
        if type(self.dirname) == list:
            self.build_files(outfile)
        else:
            self.build_dir(outfile)
    
    def build_files(self, outfile: str):
        size = 0
        ofs = []
        numfiles = 0
        for file in self.dirname:
            sz = os.stat(file).st_size
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
            b'AFS2', self.version, intsize, self.id_intsize, numfiles, self.align, self.subkey
        )

        id_strsize = f"<{self.stringtypes(self.id_intsize)}"
        for i in range(numfiles):
            header += pack(id_strsize, i)
        
        headersize = len(header) + intsize * numfiles + intsize
        aligned_header_size = headersize + (self.align - (headersize % self.align))
        ofs2 = []
        for idx, x in enumerate(ofs):
            if (x+aligned_header_size) % self.align != 0 and idx != len(ofs) - 1:
                ofs2.append((x+aligned_header_size) + (self.align - ((x+aligned_header_size) % self.align)))
            else:
                ofs2.append(x+aligned_header_size)
        ofs = [headersize] + ofs2

        for i in ofs:
            header += pack(strtype, i)
        
        if headersize % self.align != 0:
            header = header.ljust(headersize + (self.align - (headersize % self.align)), b"\x00")
        out = open(outfile, "wb")
        out.write(header)
        for idx, file in enumerate(self.dirname):
            fl = open(file, "rb").read()
            if len(fl) % self.align != 0 and idx != len(self.dirname) - 1:
                fl = fl.ljust(len(fl) + (self.align - (len(fl) % self.align)), b"\x00")
            out.write(fl)
        out.close()
    
    def build_dir(self, outfile: str):
        size = 0
        ofs = []
        numfiles = 0
        for r, d, f in os.walk(self.dirname):
            for file in f:
                sz = os.stat(os.path.join(r, file)).st_size
                if sz % self.align != 0: # Doesn't always needs to be this way?
                    sz = sz + (self.align - sz % self.align)
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
            b'AFS2', self.version, intsize, self.id_intsize, numfiles, self.align, self.subkey
        )

        id_strsize = f"<{self.stringtypes(self.id_intsize)}"
        for i in range(numfiles):
            header += pack(id_strsize, i)
        
        headersize = len(header) + intsize * numfiles + intsize
        aligned_header_size = headersize + (self.align - (headersize % self.align))
        ofs2 = []
        for idx, x in enumerate(ofs):
            if (x+aligned_header_size) % self.align != 0 and idx != len(ofs) - 1:
                ofs2.append((x+aligned_header_size) + (self.align - ((x+aligned_header_size) % self.align)))
            else:
                ofs2.append(x+aligned_header_size)
        ofs = [headersize] + ofs2

        for i in ofs:
            header += pack(strtype, i)
        
        if headersize % self.align != 0:
            header = header.ljust(headersize + (self.align - (headersize % self.align)), b"\x00")
        out = open(outfile, "wb")
        out.write(header)
        for r, d, f in os.walk(self.dirname):
            for idx, file in enumerate(f):
                fl = open(os.path.join(r, file), "rb").read()
                if len(fl) % self.align != 0 and idx != len(f) - 1:
                    fl = fl.ljust(len(fl) + (self.align - (len(fl) % self.align)), b"\x00")
                out.write(fl)
        out.close()

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