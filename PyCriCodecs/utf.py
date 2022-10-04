from typing import BinaryIO
from io import BytesIO, FileIO
from struct import unpack, calcsize, pack
from .chunk import *

# FIXME Really awful. Although works.
class UTF:
    """ Use this class to return a dict containing all @UTF chunk information. """
    __slots__ = ["magic", "table_size", "rows_offset", "string_offset", "data_offset",
                "table_name", "num_columns", "row_length", "num_rows", "stream", "table",
                "__payload", "encoding"]
    magic: bytes
    table_size: int
    rows_offset: int
    string_offset: int
    data_offset: int
    table_name: int
    num_columns: int
    row_length: int
    num_rows: int
    stream: BinaryIO
    table: dict
    __payload: list
    encoding: str
    def __init__(self, stream):
        if type(stream) == str:
            self.stream = FileIO(stream)
        else:
            self.stream = BytesIO(stream)
        self.magic, self.table_size, self.rows_offset, self.string_offset, self.data_offset, self.table_name, self.num_columns, self.row_length, self.num_rows = UTFChunkHeader.unpack(
            self.stream.read(UTFChunkHeader.size)
        )
        if self.magic == UTFType.UTF.value:
            self.table = self.read_rows_and_columns()
        elif self.magic == UTFType.EUTF.value:
            self.stream.seek(0)
            data = memoryview(bytearray(self.stream.read()))
            m = 0x655f
            t = 0x4115
            for i in range(len(data)):
                data[i] ^= (0xFF & m)
                m = (m * t) & 0xFFFFFFFF
            self.stream = BytesIO(bytearray(data))
            self.magic, self.table_size, self.rows_offset, self.string_offset, self.data_offset, self.table_name, self.num_columns, self.row_length, self.num_rows = UTFChunkHeader.unpack(
                self.stream.read(UTFChunkHeader.size)
            )
            if self.magic != UTFType.UTF.value:
                raise Exception("Decryption error.")
            self.table = self.read_rows_and_columns()
        else:
            raise ValueError("UTF chunk is not present.")
    
    def read_rows_and_columns(self) -> dict:
        stream = self.stream.read(self.data_offset-0x18)
        stream = BytesIO(stream)
        types = [[], [], [], []]
        target_data = []
        target_constant = []
        target_tuple = []
        for i in range(self.num_columns):
            flag = stream.read(1)[0]
            stflag = flag >> 4
            typeflag = flag & 0xF
            if stflag == 0x1:
                target_constant.append(int.from_bytes(stream.read(4), "big"))
                types[2].append((">"+self.stringtypes(typeflag), typeflag))
            elif stflag == 0x3:
                target_tuple.append((int.from_bytes(stream.read(4), "big"), unpack(">"+self.stringtypes(typeflag), stream.read(calcsize(self.stringtypes(typeflag))))))
                types[1].append((">"+self.stringtypes(typeflag), typeflag))
            elif stflag == 0x5:
                target_data.append(int.from_bytes(stream.read(4), "big"))
                types[0].append((">"+self.stringtypes(typeflag), typeflag))
            elif stflag == 0x7: # I've seen one article referencing this, but I never saw it personally.
                # target_tuple.append((int.from_bytes(stream.read(4), "big"), int.from_bytes(stream.read(calcsize(self.stringtypes(typeflag))), "big")))
                # types[3].append((">"+self.stringtypes(typeflag), typeflag))
                raise NotImplementedError("Unsupported 0x70 storage flag.")
            else:
                raise Exception("Unknown storage flag.")
        
        rows  = []
        table = dict()
        for j in range(self.num_rows):
            for i in types[0]:
                rows.append(unpack(i[0], stream.read(calcsize(i[0]))))

        for i in range(4):
            for j in range(len(types[i])):
                types[i][j] = (types[i][j][0][1:], types[i][j][1])
        strings = (stream.read()).split(b'\x00')
        strings_copy = strings[:]
        self.__payload = []
        self.encoding = 'utf-8'
        for i in range(len(strings)):
                try:
                    strings_copy[i] = strings[i].decode("utf-8")
                except:
                    for x in ["shift-jis", "utf-16"]:
                        try:
                            strings_copy[i] = strings[i].decode(x)
                            self.encoding = x
                            # This looks sketchy, but it will always work since @UTF only supports these 3 encodings. 
                            break
                        except:
                            continue
                    else:
                        # Probably useless.
                        raise UnicodeDecodeError(f"String of unknown encoding: {strings[i]}")
        t_t_dict = dict()
        self.table_name = strings_copy[self.finder(self.table_name, strings)]
        UTFTypeValuesList = list(UTFTypeValues)
        for i in range(len(target_constant)):
            if types[2][i][1] not in [0xA, 0xB]:
                val = self.finder(target_constant[i], strings)
                table.setdefault(strings_copy[val], []).append(0)
                t_t_dict.update({strings_copy[val]: (UTFTypeValuesList[types[2][i][1]], 0)})
            elif types[2][i][1] == 0xA:
                val = self.finder(target_constant[i], strings)
                table.setdefault(strings_copy[val], []).append("<NULL>")
                t_t_dict.update({strings_copy[val]: (UTFTypeValues.string, "<NULL>")})
            else:
                # Most likely useless, since the code doesn seem to reach here.
                val = self.finder(target_constant[i], strings)
                table.setdefault(strings_copy[val], []).append(b'')
                t_t_dict.update({strings_copy[val]: (UTFTypeValues.bytes, b'')})
        for i in range(len(target_tuple)):
            if types[1][i%(len(types[1]))][1] not in [0xA, 0xB]:
                table.setdefault(strings_copy[self.finder(target_tuple[i][0], strings)], []).append(target_tuple[i][1])
                t_t_dict.update({strings_copy[self.finder(target_tuple[i][0], strings)]: (UTFTypeValuesList[types[1][i%len(types[1])][1]], target_tuple[i][1][0])})
            elif types[1][i%(len(types[1]))][1] == 0xA:
                table.setdefault(strings_copy[self.finder(target_tuple[i][0], strings)], []).append(strings_copy[self.finder(target_tuple[i][1][0], strings)])
                t_t_dict.update({strings_copy[self.finder(target_tuple[i][0], strings)]: (UTFTypeValues.string, strings_copy[self.finder(target_tuple[i][1][0], strings)])})
            else:
                self.stream.seek(self.data_offset+target_tuple[i][1][0]+0x8, 0)
                bin_val = self.stream.read((target_tuple[i][1][1]))
                table.setdefault(strings_copy[self.finder(target_tuple[i][0], strings)], []).append(bin_val)
                t_t_dict.update({strings_copy[self.finder(target_tuple[i][0], strings)]: (UTFTypeValues.bytes, bin_val)})
        temp_dict = dict()
        if len(rows) == 0:
            self.__payload.append(t_t_dict)
        for i in range(len(rows)):
            if types[0][i%(len(types[0]))][1] not in [0xA, 0xB]:
                table.setdefault(strings_copy[self.finder(target_data[i%(len(target_data))], strings)], []).append(rows[i][0])
                temp_dict.update({strings_copy[self.finder(target_data[i%(len(target_data))], strings)]: (UTFTypeValuesList[types[0][i%(len(types[0]))][1]], rows[i][0])})
            elif types[0][i%(len(types[0]))][1] == 0xA:
                table.setdefault(strings_copy[self.finder(target_data[i%(len(target_data))], strings)], []).append(strings_copy[self.finder(rows[i][0], strings)])
                temp_dict.update({strings_copy[self.finder(target_data[i%(len(target_data))], strings)]: (UTFTypeValues.string, strings_copy[self.finder(rows[i][0], strings)])})
            else:
                self.stream.seek(self.data_offset+rows[i][0]+0x8, 0)
                bin_val = self.stream.read((rows[i][1]))
                table.setdefault(strings_copy[self.finder(target_data[i%(len(target_data))], strings)], []).append(bin_val)
                temp_dict.update({strings_copy[self.finder(target_data[i%(len(target_data))], strings)]: (UTFTypeValues.bytes, bin_val)})
            if not (i+1)%(len(types[0])):
                temp_dict.update(t_t_dict)
                self.__payload.append(temp_dict)
                temp_dict = dict()
        return table
    
    def stringtypes(self, type: int) -> str:
        types = "BbHhIiQqfdI"
        if type != 0xB:
            return types[type]
        elif type == 0xB:
            return("II")
        else:
            raise Exception("Unkown data type.")

    def finder(self, pointer, strings) -> int:
        sum = 0
        for i in range(len(strings)):
            if sum < pointer:
                sum += len(strings[i]) + 1
                continue
            return i
        else:
            raise Exception("Failed string lookup.")
    
    def get_payload(self) -> list:
        # I am a noob, but I want to standardize the table output to Donmai WannaCri's payload type.
        # Since my table parser has a different approach (an awful one at that),
        # (And it's integrated with the other tools in this lib specifically),
        # So I can't change it. However this function will return a payload list of Donmai WannaCri's type.
        # And this format can be used to build custom @UTF tables in this lib as well.
        # As for key strings, according to Donmai, they are always in ASCII encoding
        # despite, what seems to me, nothing stopping it for being any of the other 3 encodings,
        # since the header allows it.
        return self.__payload

# Revised it a bit.
class UTFBuilder:
    """ Use this class to build custom UTF tables. """
    __slots__ = ["encoding", "dictarray", "keyset", "encrypt", "encoding", "strings", 
                "table_name", "binary", "table", "rows_data", "stflag", "column_data",
                "data_offset"]
    encoding: str
    dictarray: list
    strings: bytes
    table_name: str
    binary: bytes
    table: bytearray
    stflag: list
    rows_data: bytearray
    column_data: bytearray
    data_offset: int
    
    def __init__(self, dictarray: list, encrypt: bool = False, encoding: str = "utf-8", table_name: str = "PyCriCodecs_table") -> None:
        l = set([len(x) for x in dictarray])
        if len(l) != 1:
            raise ValueError("All dictionaries must be equal in length.")
        matches = [(k, v[0]) for k, v in dictarray[0].items()]
        for i in range(1, len(dictarray)):
            if matches != [(k, v[0]) for k, v in dictarray[i].items()]:
                raise ValueError("Keys and/or value types are not matching across dictionaries.")
        self.dictarray = dictarray
        self.encrypt = encrypt
        self.encoding = encoding
        self.table_name = table_name
        self.binary = b''
        self.get_strings()

    def parse(self) -> bytearray:
        self.get_stflag()
        self.column_data = self.write_columns()
        self.rows_data = self.write_rows()
        header_data = self.write_header()
        dataarray = header_data + self.column_data + self.rows_data + self.strings + self.binary
        if len(dataarray) % 8 != 0:
            dataarray = dataarray[:8] + dataarray[8:].ljust(self.data_offset, b'\x00') # Padding.
        if self.encrypt:
            dataarray = memoryview(dataarray)
            m = 0x655f
            t = 0x4115
            for i in range(len(dataarray)):
                dataarray[i] ^= (0xFF & m)
                m = (m * t) & 0xFFFFFFFF
            dataarray = bytearray(dataarray)
        return dataarray
    
    def write_header(self) -> bytearray:
        self.data_offset = len(self.column_data) + len(self.rows_data) + len(self.strings) + len(self.binary) + 0x18
        datalen = self.data_offset
        if self.data_offset % 8 != 0:
            self.data_offset = self.data_offset + (8 - self.data_offset % 8)
        if len(self.binary) == 0:
            binary_offset = self.data_offset
        else:
            binary_offset = datalen-len(self.binary)
        header = UTFChunkHeader.pack(
                        b'@UTF', # @UTF
                        self.data_offset, # Chunk size.
                        len(self.column_data)+0x18, # Rows offset.
                        datalen-len(self.strings)-len(self.binary), # String offset.
                        binary_offset, # Binary data offset.
                        self.strings.index(b"\x00" + bytes(self.table_name, self.encoding) + b"\x00") + 1, # Table name pointer.
                        len(self.stflag), # Num columns.
                        sum([calcsize(self.stringtypes(x[1])) for x in self.stflag if x[0] == 0x50]), # Num rows.
                        len(self.dictarray) # Rows length.
                    )
        return bytearray(header)
    
    def write_rows(self) -> bytearray:
        rows = bytearray()
        for dict in self.dictarray:
            for data in self.stflag:
                if data[0] == 0x50:
                    if data[1] not in [0xA, 0xB]:
                        rows += pack(">"+self.stringtypes(data[1]), dict[data[2]][1])
                    elif data[1] == 0xA:
                        if bytes(dict[data[2]][1], self.encoding) == b"":
                            idx = self.strings.index(b'\x00\x00') + 1
                            rows += pack(">"+self.stringtypes(data[1]), idx)
                        else:
                            rows += pack(">"+self.stringtypes(data[1]), self.strings.index(b"\x00" + bytes(dict[data[2]][1], self.encoding) + b"\x00") + 1)
                    else:
                        rows += pack(">"+self.stringtypes(data[1]), self.binary.index(dict[data[2]][1]), len(dict[data[2]][1]))
        return rows

    def write_columns(self) -> bytearray:
        columns = bytearray()
        for data in self.stflag:
            columns += int.to_bytes(data[0] | data[1], 1, "big")
            if data[0] in [0x10, 0x50]:
                columns += int.to_bytes(self.strings.index(b"\x00" + bytes(data[2], self.encoding) + b"\x00") + 1, 4, "big")
            else:
                if data[1] not in [0xA, 0xB]:
                    columns += int.to_bytes(self.strings.index(b"\x00" + bytes(data[2], self.encoding) + b"\x00") + 1, 4, "big")+int.to_bytes(data[3], calcsize(self.stringtypes(data[1])), "big")
                elif data[1] == 0xA:
                    columns += int.to_bytes(self.strings.index(b"\x00" + bytes(data[2], self.encoding) + b"\x00") + 1, 4, "big")+int.to_bytes(self.strings.index(b"\x00" + bytes(data[3], self.encoding) + b"\x00") + 1, 4, "big")
                else:
                    columns += int.to_bytes(self.strings.index(b"\x00" + bytes(data[2], self.encoding) + b"\x00") + 1, 4, "big")+int.to_bytes(self.binary.index(data[3]), 4, "big")+int.to_bytes(len(data[3]), 4, "big")
        return columns

    def get_stflag(self):
        to_match = [(x, y) for x, y in self.dictarray[0].items()]
        UTFTypeValuesList = list(UTFTypeValues)
        self.stflag = []
        for val in to_match:
            if len(self.dictarray) != 1:
                for dict in self.dictarray:
                    if dict[val[0]][1] != val[1][1]:
                        self.stflag.append((0x50, UTFTypeValuesList.index(val[1][0]), val[0]))
                        break
                else:
                    if val[1][1] == 0 or val[1][1] == "<NULL>":
                        self.stflag.append((0x10, UTFTypeValuesList.index(val[1][0]), val[0]))
                    else:
                        self.stflag.append((0x30, UTFTypeValuesList.index(val[1][0]), val[0], val[1][1]))
            else:
                # It seems that when there is only one dictionary, there will be no element of type 0x30 flag
                # Otherwise all of them would be either 0x30 or 0x10 flags with no length to the rows.
                if val[1][1] == 0 or val[1][1] == "<NULL>":
                    self.stflag.append((0x10, UTFTypeValuesList.index(val[1][0]), val[0]))
                else:
                    self.stflag.append((0x50, UTFTypeValuesList.index(val[1][0]), val[0]))


    def get_strings(self):
        strings = []
        binary = b''

        for dict in self.dictarray:
            for key, value in dict.items():
                if key not in strings:
                    strings.append(key)
                if type(value[1]) == str and value[1] not in strings:
                    strings.append(value[1])
                if (type(value[1]) == bytearray or type(value[1]) == bytes) and value[1] not in binary:
                    binary += value[1]
        self.binary = binary

        strings = [self.table_name] + strings

        if "<NULL>" in strings:
            strings.pop(strings.index("<NULL>"))
            strings = ["<NULL>"] + strings

        for i in range(len(strings)):
            val = strings[i].encode(self.encoding)
            if b'\x00' in val:
                raise ValueError(f"Encoding of {self.encoding} for '{strings[i]}' results in string with a null byte.")
            else:
                strings[i] = val
        
        self.strings = b'\x00'.join(strings) + b"\x00"
        
    def stringtypes(self, type: int) -> str:
        types = "BbHhIiQqfdI"
        if type != 0xB:
            return types[type]
        elif type == 0xB:
            return("II")
        else:
            raise Exception("Unkown data type.")