import os
from io import BytesIO, FileIO
from .chunk import *
from typing import BinaryIO
from .utf import UTF, UTFBuilder
import CriCodecs

class TOC():
    __slots__ = ["magic", "unk04", "packet_size", "unk0C", "stream", "table"]
    magic: bytes
    unk04: int
    packet_size: int
    unk0C: int
    stream: BinaryIO
    table: dict
    def __init__(self, stream: bytes) -> None:
        self.stream = BytesIO(stream)
        self.magic, self.unk04, self.packet_size, self.unk0C = CPKChunkHeader.unpack(
            self.stream.read(CPKChunkHeader.size)
        )
        if self.magic not in [header.value for header in CPKChunkHeaderType]:
            raise ValueError(f"{self.magic} header not supported.")
        self.table = UTF(self.stream.read()).table

class CPK:
    __slots__ = ["magic", "unk04", "packet_size", "unk0C", "stream", "tables", "filename"]
    magic: bytes
    unk04: int
    packet_size: int
    unk0C: int
    stream: BinaryIO
    tables: dict[dict[list]]
    filename: str
    def __init__(self, filename) -> None:
        if type(filename) == str:
            self.filename = filename
            self.stream = FileIO(filename)
        else:
            self.stream = BytesIO(filename)
            self.filename = ''
        self.magic, self.unk04, self.packet_size, self.unk0C = CPKChunkHeader.unpack(
            self.stream.read(CPKChunkHeader.size)
        )
        if self.magic != CPKChunkHeaderType.CPK.value:
            raise ValueError("Invalid CPK file.")
        self.tables = dict(CPK = UTF(self.stream.read(0x800-CPKChunkHeader.size)).table)
        self.checkTocs()
    
    def checkTocs(self) -> None:
        for key, value in self.tables["CPK"].items():
            if key == "TocOffset":
                if value[0]:
                    self.stream.seek(value[0], 0)
                    self.tables["TOC"] = TOC(self.stream.read(self.tables['CPK']["TocSize"][0])).table
            elif key == "ItocOffset":
                if value[0]:
                    self.stream.seek(value[0], 0)
                    self.tables["ITOC"] = TOC(self.stream.read(self.tables['CPK']["ItocSize"][0])).table
                    if "DataL" in self.tables["ITOC"]:
                        self.tables["ITOC"]['DataL'][0] = UTF(self.tables["ITOC"]['DataL'][0]).table
                    if "DataH" in self.tables["ITOC"]:
                        self.tables["ITOC"]['DataH'][0] = UTF(self.tables["ITOC"]['DataH'][0]).table
            elif key == "HtocOffset":
                if value[0]:
                    self.stream.seek(value[0], 0)
                    self.tables["HTOC"] = TOC(self.stream.read(self.tables['CPK']["HtocSize"][0])).table
            elif key == "GtocOffset":
                if value[0]:
                    self.stream.seek(value[0], 0)
                    self.tables["GTOC"] = TOC(self.stream.read(self.tables['CPK']["GtocSize"][0])).table
            elif key == "HgtocOffset":
                if value[0]:
                    self.stream.seek(value[0], 0)
                    self.tables["HGTOC"] = TOC(self.stream.read(self.tables['CPK']["HgtocSize"][0])).table
            elif key == "EtocOffset":
                if value[0]:
                    self.stream.seek(value[0], 0)
                    self.tables["ETOC"] = TOC(self.stream.read(self.tables['CPK']["EtocSize"][0])).table
    
    def extract(self):
        if "TOC" in self.tables:
            toctable = self.tables['TOC']
            rel_off = self.tables['CPK']['TocOffset'][0]
            for i in range(len(toctable['FileName'])):
                if toctable["DirName"][i%len(toctable["DirName"])] == '':
                    dirname = self.filename.rsplit(".")[0]
                else:
                    dirname = os.path.join(self.filename.rsplit(".")[0], toctable["DirName"][i%len(toctable["DirName"])])
                os.makedirs(dirname, exist_ok=True)
                filename = toctable['FileName'][i]
                if len(filename) >= 255:
                    filename = filename[:250] + "_" + str(i) # 250 because i might be 4 digits long.
                if toctable['ExtractSize'][i] > toctable['FileSize'][i]:
                    self.stream.seek(rel_off+toctable["FileOffset"][i], 0)
                    comp_data = self.stream.read(toctable['FileSize'][i])
                    open(os.path.join(dirname, filename), "wb").write(CriCodecs.CriLaylaDecompress(comp_data))
                else:
                    self.stream.seek(rel_off+toctable["FileOffset"][i], 0)
                    open(os.path.join(dirname, filename), "wb").write(self.stream.read(toctable['FileSize'][i]))
        elif "ITOC" in self.tables:
            toctableL = self.tables["ITOC"]['DataL'][0]
            toctableH = self.tables["ITOC"]['DataH'][0]
            align = self.tables['CPK']["Align"][0]
            offset = self.tables["CPK"]["ContentOffset"][0]
            files = self.tables["CPK"]["Files"][0]
            self.stream.seek(offset, 0)
            if self.filename:
                dirname = self.filename.rsplit(".")[0]
                os.makedirs(dirname, exist_ok=True)
            else:
                dirname = ""
            for i in range(files):
                if i in toctableH['ID']:
                    idx = toctableH['ID'].index(i)
                    if toctableH['ExtractSize'][idx] > toctableH['FileSize'][idx]:
                        comp_data = self.stream.read(toctableH['FileSize'][idx])
                        open(os.path.join(dirname, str(i)), "wb").write(CriCodecs.CriLaylaDecompress(comp_data))
                    else:
                        open(os.path.join(dirname, str(i)), "wb").write(self.stream.read(toctableH['FileSize'][idx]))
                    if toctableH['FileSize'][idx] % align != 0:
                        seek_size = (align - toctableH['FileSize'][idx] % align)
                        self.stream.seek(seek_size, 1)
                elif i in toctableL['ID']:
                    idx = toctableL['ID'].index(i)
                    if toctableL['ExtractSize'][idx] > toctableL['FileSize'][idx]:
                        comp_data = self.stream.read(toctableL['FileSize'][idx])
                        open(os.path.join(dirname, str(i)), "wb").write(CriCodecs.CriLaylaDecompress(comp_data))
                    else:
                        open(os.path.join(dirname, str(i)), "wb").write(self.stream.read(toctableL['FileSize'][idx]))
                    if toctableL['FileSize'][idx] % align != 0:
                        seek_size = (align - toctableL['FileSize'][idx] % align)
                        self.stream.seek(seek_size, 1)
                
    def extract_file(self, filename):
        if "TOC" in self.tables:
            toctable = self.tables['TOC']
            rel_off = self.tables['CPK']['TocOffset'][0]
            if toctable["DirName"][0] == '':
                dirname = self.filename.rsplit(".")[0]
            else:
                dirname = os.path.join(self.filename.rsplit(".")[0], toctable["DirName"][0])
            if self.filename:
                os.makedirs(dirname, exist_ok=True)
            if filename not in toctable['FileName']:
                raise ValueError("Given filename does not exist inside the provided CPK.")
            idx = toctable['FileName'].index(filename)
            offset = rel_off+toctable["FileOffset"][idx]
            size = toctable['FileSize'][idx]
            self.stream.seek(offset, 0)
            open(os.path.join(dirname, filename), "wb").write(self.stream.read(size))
        elif "ITOC" in self.tables:
            filename = int(filename)
            toctableL = self.tables["ITOC"]['DataL'][0]
            toctableH = self.tables["ITOC"]['DataH'][0]
            alignmentsize = self.tables["CPK"]["Align"][0]
            files = self.tables["CPK"]["Files"][0]
            offset = self.tables["CPK"]["ContentOffset"][0]
            if filename in toctableL['ID']:
                idxg = toctableL['ID'].index(filename)
            elif filename in toctableH['ID']:
                idxg = toctableH['ID'].index(filename)
            else:
                raise ValueError(f"Given ID does not exist in the given CPK. ID must be smaller than {files}.")
            self.stream.seek(offset, 0)
            realOffset = offset
            for i in range(files):
                if i != filename:
                    if i in toctableH["ID"]:
                        idx = toctableH['ID'].index(i)
                        realOffset += toctableH["FileSize"][idx]
                        if toctableH["FileSize"][idx] % alignmentsize != 0:
                            realOffset += (alignmentsize - toctableH["FileSize"][idx] % alignmentsize)
                    elif i in toctableL["ID"]:
                        idx = toctableL['ID'].index(i)
                        realOffset += toctableH["FileSize"][idx]
                        if toctableL["FileSize"][idx] % alignmentsize != 0:
                            realOffset += (alignmentsize - toctableL["FileSize"][idx] % alignmentsize)
                else:
                    if self.filename:
                        dirname = self.filename.rsplit(".")[0]
                        os.makedirs(dirname)
                    else:
                        dirname = ""
                    if filename in toctableH["ID"]:
                        extractsz = toctableH['ExtractSize'][idxg]
                        flsize = toctableH['FileSize'][idxg]
                        if extractsz > flsize:
                            self.stream.seek(realOffset)
                            comp_data = self.stream.read(toctableH['FileSize'][idxg])
                            open(os.path.join(dirname, str(filename)), "wb").write(CriCodecs.CriLaylaDecompress(comp_data))
                        else:
                            open(os.path.join(dirname, str(filename)), "wb").write(self.stream.read(toctableH['FileSize'][idxg]))
                    else:
                        extractsz = toctableL['ExtractSize'][idxg]
                        flsize = toctableL['FileSize'][idxg]
                        if extractsz > flsize:
                            self.stream.seek(realOffset)
                            comp_data = self.stream.read(toctableL['FileSize'][idxg])
                            open(os.path.join(dirname, str(filename)), "wb").write(CriCodecs.CriLaylaDecompress(comp_data))
                        else:
                            open(os.path.join(dirname, str(filename)), "wb").write(self.stream.read(toctableL['FileSize'][idxg]))
                    break

class CPKBuilder:
    """ Use this class to build semi-custom CPK archives. """
    __slots__ = ["CpkMode", "Tver", "dirname", "itoc_size", "encrypt", "encoding", "files", "fileslen",
                "ITOCdata", "CPKdata", "ContentSize", "EnabledDataSize", "outfile", "TOCdata", "GTOCdata",
                "ETOCdata"]
    CpkMode: int 
    # CPK mode dictates (at least from what I saw) the use of filenames in TOC or the use of
    # ITOC without any filenames (Use of indexes only, will be sorted).
    # CPK mode of 0 = Use of ITOC only, CPK mode = 1, use of TOC, ITOC and optionally ETOC?
    Tver: str
    # Seems to be CPKMaker/CPKDLL version, I will put in one of the few ones I found as default.
    # I am not sure if this affects of modding these files.
    # However, you can change it.
    dirname: str
    itoc_size: int
    encrypt: bool
    encoding: str
    files: list
    fileslen: int
    ITOCdata: bytearray
    TOCdata: bytearray
    CPKdata: bytearray
    ContentSize: int
    EnabledDataSize: int
    outfile: str

    def __init__(self, dirname: str, outfile: str, CpkMode: int = 1, Tver: str = False, encrypt: bool = False, encoding: str = "utf-8") -> None:
        self.CpkMode = CpkMode
        if not Tver:
            # Some default ones I found with the matching CpkMode, hope they are good enough for all cases.
            if self.CpkMode == 0:
                self.Tver = 'CPKMC2.18.04, DLL2.78.04'
            elif self.CpkMode == 1:
                self.Tver = 'CPKMC2.45.00, DLL3.15.00'
            elif self.CpkMode == 2:
                self.Tver = 'CPKMC2.49.32, DLL3.24.00'
            elif self.CpkMode == 3:
                self.Tver = 'CPKFBSTD1.49.35, DLL3.24.00'
            else:
                ValueError("Unknown CpkMode.")
        else:
            self.Tver = Tver
        if dirname == "":
            raise ValueError("Invalid directory name/path.")
        self.dirname = dirname
        self.encrypt = encrypt
        self.encoding = encoding
        self.EnabledDataSize = 0
        self.ContentSize = 0
        self.outfile = outfile
        self.generate_payload()
    
    def generate_payload(self):
        if self.CpkMode == 3:
            self.TOCdata = self.generate_TOC()
            self.TOCdata = bytearray(CPKChunkHeader.pack(b'TOC ', 0xFF, len(self.TOCdata), 0)) + self.TOCdata
            self.GTOCdata = self.generate_GTOC()
            self.GTOCdata = bytearray(CPKChunkHeader.pack(b'GTOC', 0xFF, len(self.GTOCdata), 0)) + self.GTOCdata
            self.ETOCdata = self.generate_ETOC()
            self.ETOCdata = bytearray(CPKChunkHeader.pack(b'ETOC', 0xFF, len(self.ETOCdata), 0)) + self.ETOCdata
            self.CPKdata = self.generate_CPK()
            self.CPKdata = bytearray(CPKChunkHeader.pack(b'CPK ', 0xFF, len(self.CPKdata), 0)) + self.CPKdata
            data = self.CPKdata.ljust(len(self.CPKdata) + (0x800 - len(self.CPKdata) % 0x800) - 6, b'\x00') + bytearray(b"(c)CRI") + self.TOCdata.ljust(len(self.TOCdata) + (0x800 - len(self.TOCdata) % 0x800), b'\x00') + self.GTOCdata.ljust(len(self.GTOCdata) + (0x800 - len(self.GTOCdata) % 0x800), b'\x00')
            self.writetofile(data)
        elif self.CpkMode == 2:
            self.TOCdata = self.generate_TOC()
            self.TOCdata = bytearray(CPKChunkHeader.pack(b'TOC ', 0xFF, len(self.TOCdata), 0)) + self.TOCdata
            self.ITOCdata = self.generate_ITOC()
            self.ITOCdata = bytearray(CPKChunkHeader.pack(b'ITOC', 0xFF, len(self.ITOCdata), 0)) + self.ITOCdata
            self.ETOCdata = self.generate_ETOC()
            self.ETOCdata = bytearray(CPKChunkHeader.pack(b'ETOC', 0xFF, len(self.ETOCdata), 0)) + self.ETOCdata
            self.CPKdata = self.generate_CPK()
            self.CPKdata = bytearray(CPKChunkHeader.pack(b'CPK ', 0xFF, len(self.CPKdata), 0)) + self.CPKdata
            data = self.CPKdata.ljust(len(self.CPKdata) + (0x800 - len(self.CPKdata) % 0x800) - 6, b'\x00') + bytearray(b"(c)CRI") + self.TOCdata.ljust(len(self.TOCdata) + (0x800 - len(self.TOCdata) % 0x800), b'\x00') + self.ITOCdata.ljust(len(self.ITOCdata) + (0x800 - len(self.ITOCdata) % 0x800), b'\x00')
            self.writetofile(data)
        elif self.CpkMode == 1:
            self.TOCdata = self.generate_TOC()
            self.TOCdata = bytearray(CPKChunkHeader.pack(b'TOC ', 0xFF, len(self.TOCdata), 0)) + self.TOCdata
            self.CPKdata = self.generate_CPK()
            self.CPKdata = bytearray(CPKChunkHeader.pack(b'CPK ', 0xFF, len(self.CPKdata), 0)) + self.CPKdata
            data = self.CPKdata.ljust(len(self.CPKdata) + (0x800 - len(self.CPKdata) % 0x800) - 6, b'\x00') + bytearray(b"(c)CRI") + self.TOCdata.ljust(len(self.TOCdata) + (0x800 - len(self.TOCdata) % 0x800), b'\x00')
            self.writetofile(data)
        elif self.CpkMode == 0:
            self.ITOCdata = self.generate_ITOC()
            self.ITOCdata = bytearray(CPKChunkHeader.pack(b'ITOC', 0, len(self.ITOCdata), 0)) + self.ITOCdata
            self.CPKdata = self.generate_CPK()
            self.CPKdata = bytearray(CPKChunkHeader.pack(b'CPK ', 0, len(self.CPKdata), 0)) + self.CPKdata
            data = self.CPKdata.ljust(len(self.CPKdata) + (0x800 - len(self.CPKdata) % 0x800) - 6, b'\x00') + bytearray(b"(c)CRI") + self.ITOCdata.ljust(len(self.ITOCdata) + (0x800 - len(self.ITOCdata) % 0x800), b'\x00')
            self.writetofile(data)
        
    
    def writetofile(self, data) -> None:
        # All versions seem to be written the same, but just in case I forgot something, I will leave this to help.
        if self.CpkMode == 3:
            out = open(self.outfile, "wb")
            out.write(data)
            for i in self.files:
                d = open(i, "rb").read()
                if len(d) % 800 != 0:
                    d = d.ljust(len(d) + (0x800 - len(d) % 800), b"\x00")
                out.write(d)
            out.write(self.ETOCdata)
            out.close()
        elif self.CpkMode == 2:
            out = open(self.outfile, "wb")
            out.write(data)
            for i in self.files:
                d = open(i, "rb").read()
                if len(d) % 800 != 0:
                    d = d.ljust(len(d) + (0x800 - len(d) % 800), b"\x00")
                out.write(d)
            out.write(self.ETOCdata)
            out.close()
        elif self.CpkMode == 1:
            out = open(self.outfile, "wb")
            out.write(data)
            for i in self.files:
                d = open(i, "rb").read()
                if len(d) % 800 != 0:
                    d = d.ljust(len(d) + (0x800 - len(d) % 800), b"\x00")
                out.write(d)
            out.close()
        elif self.CpkMode == 0:
            out = open(self.outfile, "wb")
            out.write(data)
            for i in self.files:
                d = open(os.path.join(self.dirname, i), "rb").read()
                if len(d) % 800 != 0:
                    d = d.ljust(len(d) + (0x800 - len(d) % 800), b"\x00")
                out.write(d)
            out.close()
    
    def generate_GTOC(self) -> bytearray:
        # I have no idea why are those numbers here.
        Gdata = [
            {
                "Gname": (UTFTypeValues.string, ""),
                "Child": (UTFTypeValues.int, -1),
                "Next": (UTFTypeValues.int, 0)
            },
            {
                "Gname": (UTFTypeValues.string, "(none)"),
                "Child": (UTFTypeValues.int, 0),
                "Next": (UTFTypeValues.int, 0)
            }
        ]
        Fdata = [
            {
             "Next": (UTFTypeValues.int, -1),
             "Child": (UTFTypeValues.int, -1),
             "SortFlink": (UTFTypeValues.int, 2),
             "Aindex": (UTFTypeValues.ushort, 0)
            },
            {
             "Next": (UTFTypeValues.int, 2),
             "Child": (UTFTypeValues.int, 0),
             "SortFlink": (UTFTypeValues.int, 1),
             "Aindex": (UTFTypeValues.ushort, 0)
            },
            {
             "Next": (UTFTypeValues.int, 0),
             "Child": (UTFTypeValues.int, 1),
             "SortFlink": (UTFTypeValues.int, 2),
             "Aindex": (UTFTypeValues.ushort, 0)
            }
        ]
        Attrdata = [
            {
                "Aname": (UTFTypeValues.string, ""),
                "Align": (UTFTypeValues.ushort, 0x800),
                "Files": (UTFTypeValues.uint, 0),
                "FileSize": (UTFTypeValues.uint, 0)
            }
        ]
        payload = [
            {
                "Glink": (UTFTypeValues.uint, 2),
                "Flink": (UTFTypeValues.uint, 3),
                "Attr" : (UTFTypeValues.uint, 1),
                "Gdata": (UTFTypeValues.bytes, UTFBuilder(Gdata, encrypt=self.encrypt, encoding=self.encoding, table_name="CpkGtocGlink").parse()),
                "Fdata": (UTFTypeValues.bytes, UTFBuilder(Fdata, encrypt=self.encrypt, encoding=self.encoding, table_name="CpkGtocFlink").parse()),
                "Attrdata": (UTFTypeValues.bytes, UTFBuilder(Attrdata, encrypt=self.encrypt, encoding=self.encoding, table_name="CpkGtocAttr").parse()),
            }
        ]
        return UTFBuilder(payload, encrypt=self.encrypt, encoding=self.encoding, table_name="CpkGtocInfo").parse()

    def generate_ETOC(self) -> bytearray:
        payload = [
            {
                "UpdateDateTime": (UTFTypeValues.ullong, 0),
                "LocalDir": (UTFTypeValues.string, self.dirname)
            }
        ]
        return UTFBuilder(payload, encrypt=self.encrypt, encoding=self.encoding, table_name="CpkEtocInfo").parse()

    def generate_TOC(self) -> bytearray:
        payload = []
        self.files = []
        count = 0
        # TODO improve calculating the TOC length.
        lent = 0
        for root, dirs, files in os.walk(self.dirname): # Slows code if there's a lot of files.
            for i in files:
                lent += len(i) + 1
                count += 1
        # This estimates how large the TOC table size is. Not ideal and could error it out in rare cases.
        lent = lent + (4 + 4 + 4 + 4 + 8) * count + 32 + 9 + 30 + 6
        lent = lent + (0x800 - lent % 0x800)
        self.fileslen = count
        count = 0
        for root, dirs, files in os.walk(self.dirname):
            dirname = root.split(os.sep, 1)[1:]
            if dirname == []:
                dirname = [""]
            dirname = os.path.join(dirname[0]).replace(os.sep, "/")
            for file in files:
                self.files.append(os.path.join(root, file))
                sz = os.stat(os.path.join(root, file)).st_size
                if sz > 0xFFFFFFFF:
                    raise OverflowError("4GBs is the max size of a single file that can be bundled in a CPK archive of mode 1.")
                self.EnabledDataSize += sz
                if sz % 0x800 != 0:
                    self.ContentSize += sz + (0x800 - sz % 0x800)
                else:
                    self.ContentSize += sz
                payload.append(
                    {
                        "FileName": (UTFTypeValues.string, file),
                        "FileSize": (UTFTypeValues.uint, sz),
                        "ExtractSize": (UTFTypeValues.uint, sz),
                        "FileOffset": (UTFTypeValues.ullong, lent),
                        "ID": (UTFTypeValues.uint, count),
                        "DirName": (UTFTypeValues.string, dirname),
                        "UserString": (UTFTypeValues.string, "<NULL>")
                    }
                )
                count += 1
                lent += self.ContentSize
        return UTFBuilder(payload, encrypt=self.encrypt, encoding=self.encoding, table_name="CpkTocInfo").parse()           

    def generate_CPK(self) -> bytearray:
        if self.CpkMode == 3:
            ContentOffset = (0x800+len(self.TOCdata)+len(self.GTOCdata) + (0x800 - (0x800+len(self.TOCdata)+len(self.GTOCdata)) % 0x800))
            CpkHeader = [
                {
                    "UpdateDateTime": (UTFTypeValues.ullong, 1),
                    "ContentOffset": (UTFTypeValues.ullong, ContentOffset),
                    "ContentSize": (UTFTypeValues.ullong, self.ContentSize),
                    "TocOffset": (UTFTypeValues.ullong, 0x800),
                    "TocSize": (UTFTypeValues.ullong, len(self.TOCdata)),
                    "EtocOffset": (UTFTypeValues.ullong, self.ContentSize+ContentOffset),
                    "EtocSize": (UTFTypeValues.ullong, len(self.ETOCdata)),
                    "GtocOffset": (UTFTypeValues.ullong, (0x800+len(self.TOCdata) + (0x800 - (0x800+len(self.TOCdata)) % 0x800))),
                    "GtocSize": (UTFTypeValues.ullong, len(self.GTOCdata)),                    
                    "EnabledPackedSize": (UTFTypeValues.ullong, self.EnabledDataSize),
                    "EnabledDataSize": (UTFTypeValues.ullong, self.EnabledDataSize),
                    "Files": (UTFTypeValues.uint, self.fileslen),
                    "Groups": (UTFTypeValues.uint, 0),
                    "Attrs": (UTFTypeValues.uint, 0),
                    "Version": (UTFTypeValues.ushort, 7),
                    "Revision": (UTFTypeValues.ushort, 14),
                    "Align": (UTFTypeValues.ushort, 0x800),
                    "Sorted": (UTFTypeValues.ushort, 1),
                    "EnableFileName": (UTFTypeValues.ushort, 1),
                    "CpkMode": (UTFTypeValues.ushort, self.CpkMode),
                    "Tvers": (UTFTypeValues.string, self.Tver),
                    "Codec": (UTFTypeValues.uint, 0),
                    "DpkItoc": (UTFTypeValues.uint, 0),
                    "EnableTocCrc": (UTFTypeValues.ushort, 0),
                    "EnableFileCrc": (UTFTypeValues.ushort, 0),
                    "CrcMode": (UTFTypeValues.uint, 0),
                    "CrcTable": (UTFTypeValues.bytes, b''),
                    "FileSize": (UTFTypeValues.ullong, 0),
                    "TocCrc": (UTFTypeValues.uint, 0),
                    "HtocOffset": (UTFTypeValues.ullong, 0),
                    "HtocSize": (UTFTypeValues.ullong, 0),
                    "ItocOffset": (UTFTypeValues.ullong, 0),
                    "ItocSize": (UTFTypeValues.ullong, 0),
                    "ItocCrc": (UTFTypeValues.uint, 0),
                    "GtocCrc": (UTFTypeValues.uint, 0),
                    "HgtocOffset": (UTFTypeValues.ullong, 0),
                    "HgtocSize": (UTFTypeValues.ullong, 0),
                    "TotalDataSize": (UTFTypeValues.ullong, 0),
                    "Tocs": (UTFTypeValues.uint, 0),
                    "TotalFiles": (UTFTypeValues.uint, 0),
                    "Directories": (UTFTypeValues.uint, 0),
                    "Updates": (UTFTypeValues.uint, 0),
                    "EID": (UTFTypeValues.ushort, 0),
                    "Comment": (UTFTypeValues.string, '<NULL>'),
                }
            ]
        elif self.CpkMode == 2:
            ContentOffset = (0x800+len(self.TOCdata)+len(self.ITOCdata) + (0x800 - (0x800+len(self.TOCdata)+len(self.ITOCdata)) % 0x800))
            CpkHeader = [
                {
                    "UpdateDateTime": (UTFTypeValues.ullong, 0),
                    "ContentOffset": (UTFTypeValues.ullong, ContentOffset),
                    "ContentSize": (UTFTypeValues.ullong, self.ContentSize),
                    "TocOffset": (UTFTypeValues.ullong, 0x800),
                    "TocSize": (UTFTypeValues.ullong, len(self.TOCdata)),
                    "EtocOffset": (UTFTypeValues.ullong, self.ContentSize+ContentOffset),
                    "EtocSize": (UTFTypeValues.ullong, len(self.ETOCdata)),
                    "ItocOffset": (UTFTypeValues.ullong, (0x800+len(self.TOCdata) + (0x800 - (0x800+len(self.TOCdata)) % 0x800))),
                    "ItocSize": (UTFTypeValues.ullong, len(self.ITOCdata)),
                    "EnabledPackedSize": (UTFTypeValues.ullong, self.EnabledDataSize),
                    "EnabledDataSize": (UTFTypeValues.ullong, self.EnabledDataSize),
                    "Files": (UTFTypeValues.uint, self.fileslen),
                    "Groups": (UTFTypeValues.uint, 0),
                    "Attrs": (UTFTypeValues.uint, 0),
                    "Version": (UTFTypeValues.ushort, 7),
                    "Revision": (UTFTypeValues.ushort, 14),
                    "Align": (UTFTypeValues.ushort, 0x800),
                    "Sorted": (UTFTypeValues.ushort, 1),
                    "EnableFileName": (UTFTypeValues.ushort, 1),
                    "EID": (UTFTypeValues.ushort, 0),
                    "CpkMode": (UTFTypeValues.ushort, self.CpkMode),
                    "Tvers": (UTFTypeValues.string, self.Tver),
                    "Codec": (UTFTypeValues.uint, 0),
                    "DpkItoc": (UTFTypeValues.uint, 0),
                    "EnableTocCrc": (UTFTypeValues.ushort, 0),
                    "EnableFileCrc": (UTFTypeValues.ushort, 0),
                    "CrcMode": (UTFTypeValues.uint, 0),
                    "CrcTable": (UTFTypeValues.bytes, b''),
                    "FileSize": (UTFTypeValues.ullong, 0),
                    "TocCrc": (UTFTypeValues.uint, 0),
                    "HtocOffset": (UTFTypeValues.ullong, 0),
                    "HtocSize": (UTFTypeValues.ullong, 0),
                    "ItocCrc": (UTFTypeValues.uint, 0),
                    "GtocOffset": (UTFTypeValues.ullong, 0),
                    "GtocSize": (UTFTypeValues.ullong, 0),                    
                    "HgtocOffset": (UTFTypeValues.ullong, 0),
                    "HgtocSize": (UTFTypeValues.ullong, 0),
                    "TotalDataSize": (UTFTypeValues.ullong, 0),
                    "Tocs": (UTFTypeValues.uint, 0),
                    "TotalFiles": (UTFTypeValues.uint, 0),
                    "Directories": (UTFTypeValues.uint, 0),
                    "Updates": (UTFTypeValues.uint, 0),
                    "Comment": (UTFTypeValues.string, '<NULL>'),
                }
            ]
        elif self.CpkMode == 1:
            CpkHeader = [
                {
                    "UpdateDateTime": (UTFTypeValues.ullong, 0),
                    "ContentOffset": (UTFTypeValues.ullong, (0x800+len(self.TOCdata) + (0x800 - (0x800+len(self.TOCdata)) % 0x800))),
                    "ContentSize": (UTFTypeValues.ullong, self.ContentSize),
                    "TocOffset": (UTFTypeValues.ullong, 0x800),
                    "TocSize": (UTFTypeValues.ullong, len(self.TOCdata)),
                    "EnabledPackedSize": (UTFTypeValues.ullong, self.EnabledDataSize),
                    "EnabledDataSize": (UTFTypeValues.ullong, self.EnabledDataSize),
                    "Files": (UTFTypeValues.uint, self.fileslen),
                    "Groups": (UTFTypeValues.uint, 0),
                    "Attrs": (UTFTypeValues.uint, 0),
                    "Version": (UTFTypeValues.ushort, 7),
                    "Revision": (UTFTypeValues.ushort, 11),
                    "Align": (UTFTypeValues.ushort, 0x800),
                    "Sorted": (UTFTypeValues.ushort, 1),
                    "EnableFileName": (UTFTypeValues.ushort, 1),
                    "CpkMode": (UTFTypeValues.ushort, self.CpkMode),
                    "Tvers": (UTFTypeValues.string, self.Tver),
                    "Codec": (UTFTypeValues.uint, 0),
                    "DpkItoc": (UTFTypeValues.uint, 0),
                    "EnableTocCrc": (UTFTypeValues.ushort, 0),
                    "EnableFileCrc": (UTFTypeValues.ushort, 0),
                    "CrcMode": (UTFTypeValues.uint, 0),
                    "CrcTable": (UTFTypeValues.bytes, b''),
                    "FileSize": (UTFTypeValues.ullong, 0),
                    "TocCrc": (UTFTypeValues.uint, 0),
                    "HtocOffset": (UTFTypeValues.ullong, 0),
                    "HtocSize": (UTFTypeValues.ullong, 0),
                    "EtocOffset": (UTFTypeValues.ullong, 0),
                    "EtocSize": (UTFTypeValues.ullong, 0),
                    "ItocOffset": (UTFTypeValues.ullong, 0),
                    "ItocSize": (UTFTypeValues.ullong, 0),
                    "ItocCrc": (UTFTypeValues.uint, 0),
                    "GtocOffset": (UTFTypeValues.ullong, 0),
                    "GtocSize": (UTFTypeValues.ullong, 0),                    
                    "HgtocOffset": (UTFTypeValues.ullong, 0),
                    "HgtocSize": (UTFTypeValues.ullong, 0),
                    "TotalDataSize": (UTFTypeValues.ullong, 0),
                    "Tocs": (UTFTypeValues.uint, 0),
                    "TotalFiles": (UTFTypeValues.uint, 0),
                    "Directories": (UTFTypeValues.uint, 0),
                    "Updates": (UTFTypeValues.uint, 0),
                    "EID": (UTFTypeValues.ushort, 0),
                    "Comment": (UTFTypeValues.string, '<NULL>'),
                }
            ]
        elif self.CpkMode == 0:
            CpkHeader = [
                {
                    "UpdateDateTime": (UTFTypeValues.ullong, 0),
                    "ContentOffset": (UTFTypeValues.ullong, (0x800+len(self.ITOCdata) + (0x800 - (0x800+len(self.ITOCdata)) % 0x800))),
                    "ContentSize": (UTFTypeValues.ullong, self.ContentSize),
                    "ItocOffset": (UTFTypeValues.ullong, 0x800),
                    "ItocSize": (UTFTypeValues.ullong, len(self.ITOCdata)),
                    "EnabledPackedSize": (UTFTypeValues.ullong, self.EnabledDataSize),
                    "EnabledDataSize": (UTFTypeValues.ullong, self.EnabledDataSize),
                    "Files": (UTFTypeValues.uint, self.fileslen),
                    "Groups": (UTFTypeValues.uint, 0),
                    "Attrs": (UTFTypeValues.uint, 0),
                    "Version": (UTFTypeValues.ushort, 7), # 7?
                    "Revision": (UTFTypeValues.ushort, 0),
                    "Align": (UTFTypeValues.ushort, 0x800),
                    "Sorted": (UTFTypeValues.ushort, 0),
                    "EID": (UTFTypeValues.ushort, 0),
                    "CpkMode": (UTFTypeValues.ushort, self.CpkMode),
                    "Tvers": (UTFTypeValues.string, self.Tver),
                    "Codec": (UTFTypeValues.uint, 0),
                    "DpkItoc": (UTFTypeValues.uint, 0),
                    "FileSize": (UTFTypeValues.ullong, 0),
                    "TocOffset": (UTFTypeValues.ullong, 0),
                    "TocSize": (UTFTypeValues.ullong, 0),
                    "TocCrc": (UTFTypeValues.uint, 0),
                    "EtocOffset": (UTFTypeValues.ullong, 0),
                    "EtocSize": (UTFTypeValues.ullong, 0),
                    "ItocCrc": (UTFTypeValues.uint, 0),
                    "GtocOffset": (UTFTypeValues.ullong, 0),
                    "GtocSize": (UTFTypeValues.ullong, 0),
                    "GtocCrc": (UTFTypeValues.uint, 0),
                    "TotalDataSize": (UTFTypeValues.ullong, 0),
                    "Tocs": (UTFTypeValues.uint, 0),
                    "TotalFiles": (UTFTypeValues.uint, 0),
                    "Directories": (UTFTypeValues.uint, 0),
                    "Updates": (UTFTypeValues.uint, 0),
                    "Comment": (UTFTypeValues.string, '<NULL>'),
                }
            ]
        return UTFBuilder(CpkHeader, encrypt=self.encrypt, encoding=self.encoding, table_name="CpkHeader").parse()
    
    def generate_ITOC(self) -> bytearray:
        if self.CpkMode == 2:
            payload = []
            for i in range(len(self.files)):
                payload.append(
                    {
                        "ID": (UTFTypeValues.int, i),
                        "TocIndex": (UTFTypeValues.int, i)
                    }
                )
            return UTFBuilder(payload, encrypt=self.encrypt, encoding=self.encoding, table_name="CpkExtendId").parse()
        else:
            files = sorted(os.listdir(self.dirname), key=int)
            self.files = files
            if len(files) == 0:
                raise ValueError("No files are present in the given directory.")
            elif len(files) > 0xFFFF:
                raise OverflowError("CpkMode of 0 can only contain 65535 files at max.")
            self.fileslen = len(files)
            datal = []
            datah = []
            for i in range(len(files)):
                sz = os.stat(os.path.join(self.dirname, files[i])).st_size
                self.EnabledDataSize += sz
                if sz % 0x800 != 0:
                    self.ContentSize += sz + (0x800 - sz % 0x800)
                else:
                    self.ContentSize += sz
                if sz > 0xFFFF:
                    dicth = {
                        "ID": (UTFTypeValues.ushort, i),
                        "FileSize": (UTFTypeValues.uint, sz),
                        "ExtractSize": (UTFTypeValues.uint, sz)
                    }
                    datah.append(dicth)
                else:
                    dictl = {
                        "ID": (UTFTypeValues.ushort, i),
                        "FileSize": (UTFTypeValues.ushort, sz),
                        "ExtractSize": (UTFTypeValues.ushort, sz)
                    }
                    datal.append(dictl)
            datallen = len(datal)
            datahlen = len(datah)
            if len(datal) == 0:
                datal.append({"ID": (UTFTypeValues.ushort, 0), "FileSize": (UTFTypeValues.ushort, 0), "ExtractSize": (UTFTypeValues.ushort, 0)})
            elif len(datah) == 0:
                datah.append({"ID": (UTFTypeValues.uint, 0), "FileSize": (UTFTypeValues.uint, 0), "ExtractSize": (UTFTypeValues.uint, 0)})
            payload = [
                {
                   "FilesL" : (UTFTypeValues.uint, datallen),
                   "FilesH" : (UTFTypeValues.uint, datahlen),
                   "DataL" : (UTFTypeValues.bytes, UTFBuilder(datal, table_name="CpkItocL", encrypt=self.encrypt, encoding=self.encoding).parse()),
                   "DataH" : (UTFTypeValues.bytes, UTFBuilder(datah, table_name="CpkItocH", encrypt=self.encrypt, encoding=self.encoding).parse())
                }
            ]
            return UTFBuilder(payload, table_name="CpkItocInfo", encrypt=self.encrypt, encoding=self.encoding).parse()