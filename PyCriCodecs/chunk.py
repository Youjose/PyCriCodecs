from struct import Struct
from enum import Enum, IntFlag

UTFChunkHeader = Struct(">4sIIIIIHHI")
USMChunkHeader = Struct(">4sIBBHBBBBIIII")
CPKChunkHeader = Struct("<4sIII")

class USMChunckHeaderType(Enum):
    CRID  = b"CRID"
    SFV   = b"@SFV"
    SFA   = b"@SFA"
    ALP   = b"@ALP" # Rare.

class CPKChunkHeaderType(Enum):
    CPK   = b"CPK "
    TOC   = b"TOC "
    ITOC  = b"ITOC"
    ETOC  = b"ETOC"
    GTOC  = b"GTOC"
    HTOC  = b"HTOC"
    HGTOC = b"HGTOC"

class UTFType(Enum):
    UTF   = b"@UTF"
    EUTF  = b"\x1F\x9E\xF3\xF5" # Encrypted @UTF.

# I saw some flip the unsigned/signed indexes.
# So I am not sure what's correct or not.
class UTFTypeValues(IntFlag):
    uchar  = 0
    char   = 1
    ushort = 2
    short  = 3
    uint   = 4
    int    = 5
    ullong = 6
    llong  = 7
    float  = 8
    double = 9
    string = 10
    bytes  = 11

class reader:
    pass
