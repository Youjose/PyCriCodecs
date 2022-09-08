from ast import Str
from struct import Struct
from enum import Enum, IntFlag

UTFChunkHeader = Struct(">4sIIIIIHHI")
USMChunkHeader = Struct(">4sIBBHBBBBIIII")
CPKChunkHeader = Struct("<4sIII")
AWBChunkHeader = Struct("<4sBBHIHH")
SBTChunkHeader = Struct("<IIIII")
WavHeaderStruct = Struct("<4sI4s4sIHHIIHH")
WavSmplHeaderStruct = Struct("<4sIIIIIIIIIIIIIIII") # Supports only 1 looping point.
WavNoteHeaderStruct = Struct("<4sII")
WavDataHeaderStruct = Struct("<4sI")

class USMChunckHeaderType(Enum):
    CRID  = b"CRID" # Header.
    SFSH  = b"SFSH" # SofDec1 Header?
    SFV   = b"@SFV" # Video (VP9/H264/MPEG).
    SFA   = b"@SFA" # Audio (HCA/ADX).
    ALP   = b"@ALP" # Rare. (Alpha video information).
    CUE   = b"@CUE" # Rare. (Cue points).
    SBT   = b"@SBT" # Rare. (Subtitle information).
    AHX   = b"@AHX" # Rare. (Ahx audio file? Used for SofDec1 only?)
    USR   = b"@USR" # Rare. (User data?)
    PST   = b"@PST" # Rare. (Unknown).

class CPKChunkHeaderType(Enum):
    CPK   = b"CPK " # Header.
    TOC   = b"TOC " # Cpkmode 1, 2, 3.
    ITOC  = b"ITOC" # Cpkmode 0, 2.
    ETOC  = b"ETOC" # Cpkmode 2, 3.
    GTOC  = b"GTOC" # Cpkmode 3.
    HTOC  = b"HTOC" # Unkown.
    HGTOC = b"HGTOC"# Unkown.

class UTFType(Enum):
    UTF   = b"@UTF" # Header.
    EUTF  = b"\x1F\x9E\xF3\xF5" # Encrypted @UTF Header. Very likely exclusive to CPK's @UTF only.

class AWBType(Enum):
    AFS2  = b"AFS2" # Header.

class HCAType(Enum):
    HCA   = b"HCA\x00" # Header.
    EHCA  = b"\xC8\xC3\xC1\x00" # Encrypted HCA header.

# I saw some devs swap the unsigned/signed indexes. So I am not sure what's correct or not.
# In my own experience, swapping those results in an incorrect signed values (should be unsigned) in ACB's/CPK's.
# If someone were to change this, they must change 'stringtypes' function in UTF/UTFBuilder classes.
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