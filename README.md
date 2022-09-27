# PyCriCodecs
Python frontend with a C++ backend for managing Criware formats. 
Although for some tasks, python is used purely.

## Supporting
I am running this on Python 3.9, although other earlier versions might work


So far this lib supports in terms of:

Extracting:
- ACB/AWB (Incorrect filenames in extraction.)
- USM (Any USM there is)
- CPK (Most CPK's)

Decoding:
- ADX (All versions)
- HCA (All versions)

Building:
- CPK (All CPK modes)
- AWB (Anything)

Encoding:
- HCA (HCA Version 2.0)
- ADX (All versions, any blocksize, any HighPass Frequence, All encoding versions)

With more planned coming soon.

## Installation and Usage
To install run
```
python setup.py install
```

Note: all libs here are standardized to take either a filename/path or bytes/bytearray, so you can swap both.

Also, for audio related codecs, the looping input and output is defined in the metadata, the WAV file will not loop, but it will have a "smpl" chunk in the header, same if you want to encode a looping HCA or an ADX, the WAV must have a smpl chunk.

Otherwise it will loop normally.

### Usage:

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
##### For HCA decoding and encoding:
```python
from PyCriCodecs import *
hcaObj = HCA("filename.hca", key=0xCF222F1FE0748978) # You can change the key, or remove it if the HCA is not encrypted. Key can be a hex string.
wavfile = hcaObj.decode() # Gets you the wav file after decoding.

wavObj = HCA("filename.wav")
hcabytes = wavObj.encode(encrypt=True) # and you will get an HCA file.
# You can provide a key from when initializing, otherwise it will default to the default key, you can also encrypt keyless with keyless=true.
# You can alsoo force disable looping on HCA output by force_not_looping = True.

wavObj.encrypt()
# or
hcaObj.decrypt() 
# Any works, given it can be decrypted or encrypted as an HCA. Would do it. You can also pass a key to ".encrypt()", ".decrypt()" uses the init key. 
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
usmObj = USM("filename.cpk") # or bytes, you can add a key by key="KEYINHEXGOESHERE"
usmObj.extract() # extracts all USM contents in the current directory. You can add a directory with extract(dirname = "Example")

# You can also demux the USM internally and manage with the output bytes all you want.
usmObj.demux() # Then you have access to output property.
usmObj.output # This is a dict containing all chunks in the USM, each key has a value of a list with bytearrays.

usmObj.get_metadata() # Not for the user specifically, but if you want to look at the info inside, this is one way. 
```
##### For ACB or AWB extraction:
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
- Add ACB building.
- Add correct ACB extraction.

# Credits
- [vgmstream](https://github.com/vgmstream/vgmstream) for HCA code and some of ADX decoding code.
- [VGAudio](https://github.com/Thealexbarney/VGAudio) for some help about ADX Encoding version 4, and HCA encoding.
- [K0lb3](https://github.com/K0lb3) for helping a lot with python and Cpython, as well as helping me writing some of the code.
- [bnnm](https://github.com/bnnm) for his various contributions on audio formats, helped me a lot with adding ADX and HCA support.
- [Isaac Lozano](https://github.com/Isaac-Lozano) and his [radx](https://github.com/Isaac-Lozano/radx) (WAV -> ADX) library of which I ported into C++.
- [Nyagamon](https://github.com/Nyagamon) for a lot of what he did for criware formats.
- [donmai](https://github.com/donmai-me) and his [writeup](https://listed.to/@donmai/24921/criware-s-usm-format-part-1) of CriWare's UTF format.
- 9th for also helping me with some python knowledge.
