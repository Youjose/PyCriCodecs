# PyCriCodecs
Python frontend with a C++ backend for managing Criware formats. 
Although for some tasks, python is used purely.
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
##### For HCA decoding:
```python
from PyCriCodecs import *
hcaObj = HCA("filename.hca", key=0xCF222F1FE0748978) # You can change the key, or remove it if the HCA is not encrypted.
wavfile = hcaObj.decode() # Gets you the wav file after decoding.
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

usmObj.get_metadata() # Not for the user specifically, but if you want to look at the info inside, this is one way. 
```
##### For ACB or AWB extraction (No support for subkey decryption yet.):
```python
from PyCriCodecs import *
# ACB Extraction:
acbObj = ACB("filename.acb") # It will attempt to open "filename.awb" as well if there are no sub-banks in the ACB.
acbObj.extract(dirname="dirname", decode=True, key=key) # You can turn off decoding by decode=False.
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
- Add HCA encoding.
- Add ACB building.
- And many more.

# Credits
- [vgmstream](https://github.com/vgmstream/vgmstream) for HCA code and some of ADX decoding code.
- [K0lb3](https://github.com/K0lb3) for helping a lot with python and Cpython, as well as helping me writing some of the code.
- [bnnm](https://github.com/bnnm) for his various contributions on audio formats, helped me a lot with adding ADX and HCA support.
- [Isaac Lozano](https://github.com/Isaac-Lozano) and his [radx](https://github.com/Isaac-Lozano/radx) (WAV -> ADX) library of which I ported into C++.
- [Nyagamon](https://github.com/Nyagamon) for a lot of what he did for criware formats.
- [donmai](https://github.com/donmai-me) and his [writeup](https://listed.to/@donmai/24921/criware-s-usm-format-part-1) of CriWare's UTF format.
- 9th for also helping me with some python knowledge.
