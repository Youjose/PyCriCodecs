# PyCriCodecs
Python frontend with a C++ backend for managing Criware formats.
## Installation and Usage
To install run
```
python setup.py install
```

Usage:
Note: all libs here are standardized to take either a filename/path or bytes/bytearray, so you can swap both.
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
CpkObj.extract_file() # Extract a file from a given filename (or an ID for CPKMode 0)

# Building:
CPKBuilder("dirname", "outfile.cpk", CpkMode=1) # CpkMode is important sometimes, get your target mode by extracting a sample table. 
# Given a directory, it will take that directory as root, and builds a CPK for the directories and files inside.
# Output would be a cpk file as specified.
```
##### For USM extraction:
```python
from PyCriCodecs import *
# Extraction:
usmObj = USM("filename.cpk") # or bytes, you can add a key by key="KEYINHEXGOESHERE" must be padded to 8 characters with 0's if not.
usmObj.extract() # extracts all USM contents in the current directory. You can add a directory with extract(dirname = "Example")

# You can also demux the USM internally and manage with the output bytes all you want.
usmObj.demux() # Then you have access to output property.
usmObj.output # This is a dict containing all chunks in the USM, each key has a value of a list with bytearrays.

# Experimental: USM metadata, this is preparation for USM building, but it returns a dict of dicts, of list which has dicts...
# Quite complicated but it has all metadata in the USM, it's a payload basically based off Donmai's WannaCri, same with UTF chunks, but that is made and done.
usmObj.get_metadata() # Not for the user specifically, but if you want to look at the info inside, this is one way. 
```
##### For ACB or AWB extraction (No support for subkey decryption yet.):
```python
from PyCriCodecs import *
# ACB Extraction:
acbObj = ACB("filename.acb") # It will attempt to open "filename.awb" as well if there are no sub-banks in the ACB.
acbObj.extract() # You can add dirname for extraction with dirname="dirname".

# AWB Extraction:
awbObj = AWB("filename.awb")
# You can either loop through the audios inside with:
for file in awbObj.getfiles():
    file # file bytes.
    open("file.hca or anything", wb).write(file)

# or you can call the extract function, not advised.
awbObj.extract()
```

Check the [Wiki](https://github.com/LittleChungi/PyCriCodecs/wiki/Docs-and-Thoughts) for my thoughts, plans, more options, and some details as well for documentation.

## TODO List
- Add USM building.
- Add HCA decoding and encoding.
- Add AWB/ACB full extraction for all versions, and ACB building as well.
- And many more.
