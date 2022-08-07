# PyCriCodecs
Python frontend with a C++ backend for managing Criware formats.
## Installation and Usage
To install run
```
python setup.py install
```

Usage:
##### For ADX decoding and encoding:
```python
from PyCriCodecs import *
# Decoding:
AdxObj = ADX("sample.adx") # Specify filename or adx bytes.
wavfilebytes = AdxObj.decode() # Decode will return a bytearray containing decoded ADX data as a wav file.

# Encoding:
WavObj = ADX("sample.wav") # Wav file or wav file bytes, any works.
adxbytes = WavObj.encode() # Returns an ADX file as bytes, check the wiki for more options.
```
##### For CPK extraction and building:
```python
from PyCriCodecs import *
# Extraction:
CpkObj = CPK("filename.cpk")
CpkObj.extract() # Will extract files to a dir names "filename"
CpkObj.extract_file() # Extract a file from a given filename (or an ID for CPKMode 1)

# Building:
CPKBuilder("dirname", "outfile.cpk", CpkMode=1) # CpkMode is important sometimes, get your target mode by extracting a sample table. 
# Given a directory, it will take that directory as root, and builds a CPK for the directories and files inside.
# Output would be a cpk file as specified.
```

Check the [Wiki](https://github.com/LittleChungi/PyCriCodecs/wiki/Docs-and-Thoughts) for my thoughts, plans, more options, and some details as well for documentation.

## TODO List
- Complete the USM lib, shouldn't be hard.
- Add HCA decoding and encoding.
- Add AWB/ACB extraction and building.
- And many more.
