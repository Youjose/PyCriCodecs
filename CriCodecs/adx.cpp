#pragma once
#include <cmath>
#include <iostream>
#include "pcm.cpp"
#include <cstring>
#define PI    3.141592653589793
#define SQRT2 1.414213562373095

char AdxErrorCode = 0;

static const char* AdxErrorStrings[] = {
    "Invalid ADX file header.", // -1
    "AHX file provided, unsopported.", // -2
    "Encrypted ADX detected, unsupported.", // -3
    "Invalid/Unknown encoding mode found.", // -4
    "Unknown ADX version provided.", // -5
    "Invalid Bitdepth found on the provided ADX.", // -6
    "ADX does not contain any channels info.", // -7
    "Invalid ADX header, loop information size is bigger than the header.", // -8
    "Inavlid ADX header, Criware copyright string not found.", // -9
    "Numbers of Channel cannot exceed 255 or go below 0.", // -10
    "Bitdepth must be between 2 and 15 inclusive.", // -11
    "Blocksize must be between 3 and 255 inclusive.", // -12
    "EncodingMode must be either 2, 3, or 4.", // -13
    "HighpassFrequency must be between 0 and 65535 inclusive.", // -14
    "Filter is used with EncodingMode == 2 and must be between 0 and 4 inclusive.", // -15
    "AdxVersion must be either 3, 4 or 5.", // -16
    "Provided Bitdepth does not fit correctly with the provided BlockSize", // -17
    "Given WAVE file is not valid for ADX encoding." // -18
};

void PyAdxSetError(char ErrorCode){
    ErrorCode = std::abs(ErrorCode);
    if(ErrorCode == 3)
        PyErr_SetString(PyExc_NotImplementedError, AdxErrorStrings[ErrorCode - 1]);
    else
        PyErr_SetString(PyExc_ValueError, AdxErrorStrings[ErrorCode - 1]);
}

static const unsigned char MultiplyDeBruijnBitPosition[] = {
    0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
    8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31
};

static const signed short StaticCoefficients[8] = {0x0000,0x0000,0x0F00,0x0000,0x1CC0,(signed short)0xF300,0x1880,(signed short)0xF240};

static const char* CRIString = "(c)CRI";

static inline int log2_mod_from_VGAudio(int value){
    value |= value >> 0x01;
    value |= value >> 0x02;
    value |= value >> 0x04;
    value |= value >> 0x08;
    value |= value >> 0x10;
    return (int)MultiplyDeBruijnBitPosition[(unsigned int)(value * 0x07C4ACDDU) >> 27];
}

static inline void CalculateCoefficients(int *&Coefficients, unsigned short &HighpassFrequency, unsigned int &SampleRate){
    double a = SQRT2 - cos(2.0 * PI * HighpassFrequency / SampleRate);
    double b = SQRT2 - 1;
    double c = (a - sqrt((a + b) * (a - b))) / b;
    Coefficients[0] = (int)(c * 8192);
    Coefficients[1] = (int)(c * c * -4096);
}

struct ADPCMHistory{
    short Previous1 = 0;
    short Previous2 = 0;
    void loadADPCMHistory(unsigned char* data){
        Previous1 = ReadShortBE(data+0);
        Previous2 = ReadShortBE(data+2);
    }
    void writeADPCMHistory(unsigned char* data){
        WriteShortBE(data+0, Previous1);
        WriteShortBE(data+2, Previous2);
    }
};

struct ADXLoop{
    unsigned short LoopIndex;
    unsigned short LoopType;
    unsigned int   LoopStartSample;
    unsigned int   LoopStartByte;
    unsigned int   LoopEndSample;
    unsigned int   LoopEndByte;
    void loadLoop(unsigned char* data){
        LoopIndex       = ReadUnsignedShortBE(data+0);
        LoopType        = ReadUnsignedShortBE(data+2);
        LoopStartSample = ReadUnsignedIntBE(data+4);
        LoopStartByte   = ReadUnsignedIntBE(data+8);
        LoopEndSample   = ReadUnsignedIntBE(data+12);
        LoopEndByte     = ReadUnsignedIntBE(data+16);
    }
    void writeLoop(unsigned char* data, unsigned int AlignmentSamples, smplloop Loop, unsigned int BlockSize, unsigned int Index, unsigned int Channels, unsigned int HeaderSize, unsigned int SamplesPerFrame){
        unsigned int start = Loop.Start + AlignmentSamples;
        unsigned int end   = Loop.End + AlignmentSamples;
        start = HeaderSize + ((start / SamplesPerFrame) * BlockSize) * Channels;
        end   = HeaderSize + GetNextMultiple(((end / SamplesPerFrame) * BlockSize + (end % SamplesPerFrame) / BlockSize), BlockSize) * Channels;
        WriteShortBE(data+0, Index);
        WriteShortBE(data+2, 1); /* Enable all loops. */
        WriteIntBE(data+4, Loop.Start + AlignmentSamples);
        WriteIntBE(data+8, start);
        WriteIntBE(data+12, Loop.End  + AlignmentSamples);
        WriteIntBE(data+16, end);
    }
};

struct Loop{
    unsigned short AlignmentSamples;
    unsigned short LoopCount;
    ADXLoop *Loops;
    Loop(){
        Loops = NULL;
    }
    ~Loop(){
        if(Loops != NULL)
            delete[] Loops;
    }
    char loadLoops(unsigned char* data, unsigned short &DataOffset, unsigned int &BaseOffset){
        AlignmentSamples = ReadUnsignedShortBE(data+0);
        LoopCount        = ReadUnsignedShortBE(data+2);
        if(!LoopCount)
            return 0;
        else if(BaseOffset + 4 + (LoopCount * sizeof(ADXLoop)) >= DataOffset - 2)
            return -8;
        Loops = new ADXLoop[LoopCount];
        for(unsigned int i = 0, offset = 4; i < LoopCount; i++, offset+=sizeof(ADXLoop))
            Loops[i].loadLoop(data+offset);
        return 0;
    }
    void writeLoops(unsigned char* data, unsigned int NumberOfLoops, unsigned int ChannelCount, smpl &PCMsmpl, unsigned int BlockSize, unsigned int SamplesPerFrame, unsigned int HeaderSize){
        LoopCount = NumberOfLoops;
        unsigned int start = PCMsmpl.Loops[0].Start;
        unsigned int SamplesInFrame = (BlockSize - 2) * 2;
        AlignmentSamples = GetNextMultiple(start, ChannelCount == 1 ? SamplesInFrame * 2 : SamplesInFrame);
        WriteShortBE(data+0, AlignmentSamples);
        WriteShortBE(data+2, LoopCount);
        Loops = new ADXLoop[LoopCount]; /* From what I've seen, ADX doesn't support more than one loop, but this code is already generic as is, so I will allow more customizability. */
        for(unsigned int i = 0, offset = 4; i < LoopCount; i++, offset+=sizeof(ADXLoop)){
            Loops[i].writeLoop(data+offset, AlignmentSamples, PCMsmpl.Loops[i], BlockSize, i, ChannelCount, HeaderSize, SamplesPerFrame);
        }
    }
};

struct ADXHeader{
    unsigned short Signature;         /* 0x8000 */
    unsigned short DataOffset;        /* Good to standardize, but made variable here for generic purposes. */
    unsigned char  EncodingMode;      /* Fixed = 2, Linear = 3, Exponential = 4 */
    unsigned char  BlockSize;         /* 0x12 standardized in ADXPlay, but made variable in this encoder and will pad the PCM data if needed, change with care. */
    unsigned char  BitDepth;          /* Fixed at 4 in ADXPlay, but made variable here as long as it fits into BlockSize, change with care. */
    unsigned char  Channels;          /* Same as input PCM */
    unsigned int   SampleRate;        /* Same as input PCM */
    unsigned int   SampleCount;       /* Same as input PCM, calculated for per channel in ADX. */
    unsigned short HighpassFrequency; /* Cutoff Frequency. Not sure what it does, saw some say it alleviates noise when compressing. */
    unsigned char  AdxVersion;        /* 3, 4, or 5. 3 is an old version, 4 is most used, 5 does not support Looping it seems. */
    unsigned char  Flag;              /* 8 or 9 for encryption, unsupported if so. AHX can also be encrypted. */
    void loadHeader(unsigned char* data){
        Signature         = ReadUnsignedShortBE(data+0);
        DataOffset        = ReadUnsignedShortBE(data+2);
        EncodingMode      = *(data+4);
        BlockSize         = *(data+5);
        BitDepth          = *(data+6);
        Channels          = *(data+7);
        SampleRate        = ReadUnsignedIntBE(data+8);
        SampleCount       = ReadUnsignedIntBE(data+12);
        HighpassFrequency = ReadUnsignedShortBE(data+16);
        AdxVersion        = *(data+18);
        Flag              = *(data+19);
    }
    void writeHeader(unsigned char* data, unsigned int DataOffset, unsigned int EncodingMode, unsigned int BlockSize, unsigned int BitDepth, unsigned int Channels, unsigned int SampleRate, unsigned int SampleCount, unsigned int HighpassFrequency, unsigned int AdxVersion, unsigned int Flag){
        WriteShortBE(data+0x00, 0x8000);
        WriteShortBE(data+0x02, DataOffset);
        WriteChar(data+0x04,    EncodingMode);
        WriteChar(data+0x05,    BlockSize);
        WriteChar(data+0x06,    BitDepth);
        WriteChar(data+0x07,    Channels);
        WriteIntBE(data+0x08,   SampleRate);
        WriteIntBE(data+0x0C,   SampleCount);
        WriteShortBE(data+0x10, HighpassFrequency);
        WriteChar(data+0x12,    AdxVersion);
        WriteChar(data+0x13,    Flag);
    }
};

struct ChannelFrame{
    ADPCMHistory History;
    unsigned int Position = 0;
    short* data;
    void Decode(BitReader &Reader, int Scale, unsigned int SamplesPerBlock, int* Coefficients, ADXHeader Header){
        int Sample;
        char Predictor;
        switch(Header.EncodingMode){
        case 4:
            Scale = 1 << (12 - Scale);
            break;
        case 2:
            Predictor = Scale >> 13;
            Scale = (Scale & 0x1FFF) + 1;
            Coefficients[0] = StaticCoefficients[Predictor*2 + 0];
            Coefficients[1] = StaticCoefficients[Predictor*2 + 1];
            break;
        default:
            Scale += 1;
            break;
        }
        for(unsigned int i = 0; i < SamplesPerBlock; i++, Position+=Header.Channels){
            Sample            = Reader.ReadSignedInt(Header.BitDepth);
            Sample            = Sample * Scale + (Coefficients[0] * (int)History.Previous1 >> 12) + (Coefficients[1] * (int)History.Previous2 >> 12);
            Sample            = Clamp(Sample, 0x7FFF);
            data[Position]    = Sample;
            History.Previous2 = History.Previous1;
            History.Previous1 = Sample;
        }
    }
    void Encode(BitWriter &Writer, unsigned int BlockSize, unsigned int SamplesPerBlock, int *Coefficients, fmt *&Wavefmt, unsigned int &BitDepth, unsigned int &EncodingMode, unsigned int &Filter){
        unsigned int Power;
        int Minimum = 0, Maximum = 0, Limit = (1 << (BitDepth - 1)) - 1;
        int *Samples = new int[SamplesPerBlock]; 
        short Original1 = History.Previous1, Original2 = History.Previous2;

        for(unsigned int i = 0; i < SamplesPerBlock; i++, Position += Wavefmt->Channels){
            int Sample = (((int)data[Position] << 12) - Coefficients[0] * History.Previous1 - Coefficients[1] * History.Previous2) >> 12;
            if(Sample < Minimum)
                Minimum = Sample;
            else if(Sample > Maximum)
                Maximum = Sample;
            Samples[i] = data[Position];
            History.Previous2 = History.Previous1;
            History.Previous1 = data[Position];
        }
        if(!Minimum && !Maximum){
            memset(Writer.Buffer, 0, BlockSize);
            return;
        }

        unsigned short Scale = Maximum/Limit > Minimum/~Limit ? Maximum/Limit : Minimum/~Limit;
        if(Scale > 0x1000)
            Scale = 0x1000;

        switch(EncodingMode){
        case 4:
            Power = Scale == 0 ? 0 : log2_mod_from_VGAudio(Scale) + 1;
            Scale = 1 << Power;
            Writer.Write(12 - Power, 16);
            break;
        case 2:
            Writer.Write(Filter | (Scale & 0x1FFF), 16);
            break;
        default:
            Writer.Write(Scale, 16);
            break;
        }

        auto WriteBits = [&](int Sample){
            int Delta = ((Sample << 12) - Coefficients[0] * History.Previous1 - Coefficients[1] * History.Previous2) >> 12;
            if(!Scale)
                Scale = 1;
            Delta = Delta > 0 ? Delta + (Scale >> 1) : Delta - (Scale >> 1);
            Delta /= Scale;
            Delta = Clamp(Delta, Limit);
            int SimulatedSample = ((Delta << 12) * Scale + Coefficients[0] * History.Previous1 + Coefficients[1] * History.Previous2) >> 12;
            SimulatedSample = Clamp(SimulatedSample, 0x7FFF);
            History.Previous2 = History.Previous1;
            History.Previous1 = (short)SimulatedSample;
            Writer.Write(Delta, BitDepth);
        };

        History.Previous1 = Original1;
        History.Previous2 = Original2;
        for(unsigned int i = 0; i < SamplesPerBlock; i++)
            WriteBits(Samples[i]);
        delete[] Samples;
    }
};

struct ADX{
    ADXHeader Header;
    Loop LoopPoints;
    bool Looping;
    unsigned int DataBlockSize;
    unsigned int BitsPerBlock;
    unsigned int SamplesPerBlock;
    int *Coefficients;
    ChannelFrame *Channels;
    ADPCMHistory *History;
    short *PCMData;
    unsigned int size;
    ADX(){
        Coefficients = NULL;
        Channels = NULL;
        History = NULL;
    }
    ~ADX(){
        if(Coefficients != NULL) delete[] Coefficients;
        if(Channels != NULL) delete[] Channels;
        if(History != NULL) delete[] History;
    }
    char loadHeader(unsigned char* data){
        unsigned int BaseOffset  = sizeof(ADXHeader);
        Header.loadHeader(data);

        if(Header.Signature != 0x8000) 
            return -1; /* Header magic is 0x8000 */
        else if(Header.EncodingMode == 0x11 || Header.EncodingMode == 0x10 || Header.AdxVersion == 0x06 || Header.BlockSize == 0x00 || Header.BitDepth == 0x00)
            return -2; /* This is for AHX-DC and AHX respectively, which uses MPEG-II with a custom frame size. */
        else if(Header.Flag == 0x08 || Header.Flag == 0x09)
            return -3; /* Encrypted ADX/AHX unsupported as it stands. */
        else if(Header.EncodingMode != 2 && Header.EncodingMode != 3 && Header.EncodingMode != 4)
            return -4; /* Unknown modes besides those for ADX. */
        else if(Header.AdxVersion != 3 && Header.AdxVersion != 4 && Header.AdxVersion != 5)
            return -5; /* Unknown versions besides those for ADX. */
        else if(((Header.BlockSize - 2) * 8) % Header.BitDepth != 0 || Header.BitDepth >= 16)
            return -6; /* Wouldn't make any sense for the compressed sample to be bigger than the original sample in size. */
        else if(Header.Channels == 0){
            return -7; /* Must have channels. */
        }

        if(Header.AdxVersion == 0x05)
            Looping = 0;
        else if(Header.AdxVersion == 0x04){
            BaseOffset += 4; // Padding
            History = new ADPCMHistory[Header.Channels > 1 ? Header.Channels : 2]; // Fixed size for 1 channel.
            for(unsigned int i = 0, z = 0; i < Header.Channels; i++, z+=4)
                History[i].loadADPCMHistory(data+BaseOffset+z);
            BaseOffset += (Header.Channels > 1 ? sizeof(ADPCMHistory) * Header.Channels : sizeof(ADPCMHistory) * 2);
            if(BaseOffset + 24 <= Header.DataOffset - 2)
                Looping = 1;
        }else{ /* Header.AdxVersion == 0x03 */
            if(BaseOffset + 24 <= Header.DataOffset - 2)
                Looping = 1;
        }

        if(Looping){
            AdxErrorCode = LoopPoints.loadLoops(data+BaseOffset, Header.DataOffset, BaseOffset);
            if(AdxErrorCode < 0)
                return AdxErrorCode;
            else if(!LoopPoints.LoopCount)
                Looping = 0;
        }

        DataBlockSize = Header.BlockSize - 2;
        BitsPerBlock = DataBlockSize * 8;
        SamplesPerBlock = BitsPerBlock / Header.BitDepth;

        for(unsigned char i = 0; i < 7; i++){
            if(CRIString[i] != *(data + Header.DataOffset - 2 + i))
                return -9;
        }

        Channels = new ChannelFrame[Header.Channels];
        if(Header.AdxVersion == 0x04){
            for(unsigned int i = 0; i < Header.Channels; i++)
                Channels[i].History = History[i];
        }

        Coefficients = new int[2];
        return 0;
    }
    void writeHeader(unsigned char*& AdxData, unsigned int HeaderSize, PCM &PCMMain, unsigned int BitDepth, unsigned int BlockSize, unsigned int EncodingMode, unsigned short HighpassFrequency, unsigned int SamplesPerChannel, unsigned int AdxVersion){
        Header.writeHeader(AdxData, HeaderSize-4, EncodingMode, BlockSize, BitDepth, PCMMain.wav.chunks.WAVEfmt.Channels, PCMMain.wav.chunks.WAVEfmt.SampleRate, SamplesPerChannel, EncodingMode == 2 ? 0 : HighpassFrequency, AdxVersion, 0);
        unsigned int BaseOffset = sizeof(ADXHeader);
        unsigned int FrameSizeBytes = (BlockSize - 2) * 8;
        unsigned int SamplesPerFrame = FrameSizeBytes / BitDepth;

        if(AdxVersion == 0x04 || AdxVersion == 0x05){
            WriteIntBE(AdxData+BaseOffset, 0); /* Padding */
            for(unsigned int i = 0, Offset = 4; i < PCMMain.wav.chunks.WAVEfmt.Channels; i++, Offset+=sizeof(ADPCMHistory))
                Channels[i].History.writeADPCMHistory(AdxData+BaseOffset+Offset);
            BaseOffset += 4 + (PCMMain.wav.chunks.WAVEfmt.Channels > 1 ? sizeof(ADPCMHistory) * PCMMain.wav.chunks.WAVEfmt.Channels : sizeof(ADPCMHistory) * 2);
        }

        if(Looping){
            LoopPoints.writeLoops(AdxData+BaseOffset, PCMMain.wav.chunks.WAVEsmpl.NumberofSampleLoops, PCMMain.wav.chunks.WAVEfmt.Channels, PCMMain.wav.chunks.WAVEsmpl, BlockSize, SamplesPerFrame, HeaderSize);
            BaseOffset += 4 + (sizeof(ADXLoop) * PCMMain.wav.chunks.WAVEsmpl.NumberofSampleLoops);
        }

        for(unsigned int i = 0; i < 7; i++)
            WriteChar(AdxData+HeaderSize+i-6, CRIString[i]);
    }
    char Decode(unsigned char*& data, PCM& wav){
        AdxErrorCode = loadHeader(data);
        if(AdxErrorCode < 0)
            return AdxErrorCode;
        
        unsigned int BaseOffset = Header.DataOffset + 4;
        unsigned int Blocks     = ceilf((float)Header.SampleCount / (float)SamplesPerBlock);
        unsigned int HeaderSize = 44;
        BitReader Reader;
        CalculateCoefficients(Coefficients, Header.HighpassFrequency, Header.SampleRate);

        if(Looping){
            wav.wav.chunks.Looping = 1;
            wav.wav.chunks.WAVEsmpl.Loops = new smplloop[1]; // Could be LoopPoints.LoopCount, but not really logical and the PCM module doesn't support it.
            wav.wav.chunks.WAVEsmpl.Loops[0].Start = LoopPoints.Loops[0].LoopStartSample;
            wav.wav.chunks.WAVEsmpl.Loops[0].End = LoopPoints.Loops[0].LoopEndSample;
        }
        wav.wav.chunks.WAVEdata.size = Header.SampleCount * Header.Channels * sizeof(short);
        wav.GetWaveBuffer(Header.SampleCount, Header.Channels, Header.SampleRate, Looping);
        PCMData = wav.PCM_16;

        for(unsigned int i = 0; i < Header.Channels; i++)
            Channels[i].data = PCMData+i;

        for(unsigned int i = 0; i < Blocks; i++){
            if(*(data+BaseOffset) == 0x80 && *(data+BaseOffset+1) == 0x01)
                break; /* EOF Scale. */
            
            for(unsigned int j = 0; j < Header.Channels; j++, BaseOffset += Header.BlockSize){
                int Scale = ReadUnsignedShortBE(data+BaseOffset);
                Reader.SetBuffer(data+BaseOffset+2, DataBlockSize);
                Channels[j].Decode(Reader, Scale, SamplesPerBlock, Coefficients, Header);
            }
        }
        return 0;
    }
    char Encode(PCM &PCMMain, unsigned char *&AdxData, unsigned int BitDepth, unsigned int BlockSize, unsigned int EncodingMode, unsigned short HighpassFrequency, unsigned int Filter, unsigned int AdxVersion, bool ForceNoLooping){
        fmt *Wavefmt = &PCMMain.wav.chunks.WAVEfmt;
        unsigned char ChannelCount = Wavefmt->Channels;
        unsigned int SampleRate = Wavefmt->SampleRate;
        unsigned int SampleCount = PCMMain.ColumnSize;
        unsigned int BlocksOffset;
        Looping = ForceNoLooping && AdxVersion == 0x05 ? false : PCMMain.wav.chunks.Looping;
        
        if(ChannelCount > 255 || ChannelCount < 1){
            return -10; /* I think internally it is limited at 8, but the header allowed a full char, so why not. */
        }else if(BitDepth <= 1 || BitDepth >= 16){
            return -11; /* Can't be 1 otherwise the maths won't work, and too high has no meaning in compression. */
        }else if(BlockSize <= 2 || BlockSize > 255){
            return -12; /* Can't be lower than 2 since the Scale + Predictor takes 2 bytes, can't go over 255 since it is a char. */
        }else if(EncodingMode != 2 && EncodingMode != 3 && EncodingMode != 4){
            return -13; /* Only supported modes. */
        }else if(HighpassFrequency < 0 || HighpassFrequency > 0xFFFF){
            return -14; /* No idea what this does exactly, saw some say to tweak this if noise was appearing in the ADX. */
        }else if(Filter != 0 && Filter != 1 && Filter != 2 && Filter != 3){
            return -15; /* Those are used as Predictors for the Coefficients. */
        }else if(AdxVersion != 3 && AdxVersion != 4 && AdxVersion != 5){
            return -16; /* Only supported versions. */
        }else if((8 * (BlockSize - 2)) % BitDepth != 0){
            return -17; /* BitsPerBlock must be divisible by the BitDepth. */
        }else if(SampleCount < ChannelCount || SampleCount % ChannelCount != 0){
            return -18; /* Checking if the PCM is valid for encoding. If the DataBlockSize is not divisible per channel's sample size, it will pad it. */
        }

        DataBlockSize = BlockSize - 2;
        BitsPerBlock = DataBlockSize * 8;
        SamplesPerBlock = BitsPerBlock / BitDepth;
        unsigned int SamplesPerChannel = SampleCount / ChannelCount, Frames, Blocks, HeaderSize;
        bool own = false;

        if(SamplesPerChannel % SamplesPerBlock != 0){
            unsigned int SamplesNeeded = GetNextMultiple(SamplesPerChannel, DataBlockSize) * ChannelCount;
            Frames = (SamplesNeeded / ChannelCount) / SamplesPerBlock;
            PCMData = new short[SamplesNeeded];
            own = true;
            memcpy(PCMData, PCMMain.Get_PCM16(), SampleCount * sizeof(short));
            memset(PCMData+SampleCount, 0, (SamplesNeeded-SampleCount) * sizeof(short));
        }else{
            PCMData = PCMMain.Get_PCM16();
            Frames = SamplesPerChannel / SamplesPerBlock;
        }
        Coefficients = new int[2];
        if(EncodingMode == 2){
            Coefficients[0] = (int)StaticCoefficients[Filter * 2 + 0];
            Coefficients[1] = (int)StaticCoefficients[Filter * 2 + 1];
        }else
            CalculateCoefficients(Coefficients, HighpassFrequency, SampleRate);

        Filter <<= 13;
        Channels = new ChannelFrame[ChannelCount];
        BitWriter Writer;
        for(unsigned int i = 0; i < ChannelCount; i++){
            Channels[i].data = PCMData+i;
            if(AdxVersion == 0x04 || AdxVersion == 0x05){
                Channels[i].History.Previous1 = *(PCMData + i);
                Channels[i].History.Previous2 = *(PCMData + i);
            }
        }

        Blocks = Frames * ChannelCount;
        HeaderSize = sizeof(ADXHeader) + 6; /* 6 is CRIString length. */
        if(AdxVersion == 0x04 || AdxVersion == 0x05)
            HeaderSize += (Header.Channels > 1 ? sizeof(ADPCMHistory) * Header.Channels : sizeof(ADPCMHistory) * 2);
        if(Looping)
            HeaderSize += 4 + PCMMain.wav.chunks.WAVEsmpl.NumberofSampleLoops * sizeof(ADXLoop);
        HeaderSize = HeaderSize % 16 == 0 ? HeaderSize : HeaderSize + (16 - (HeaderSize % 16)); /* Padding, not necessary but why not. */
        size = HeaderSize + (Blocks * BlockSize) + BlockSize;
        AdxData = new unsigned char[size];
        memset(AdxData, 0, HeaderSize);
        writeHeader(AdxData, HeaderSize, PCMMain, BitDepth, BlockSize, EncodingMode, HighpassFrequency, SamplesPerChannel, AdxVersion);
        BlocksOffset = HeaderSize;

        for(unsigned int i = 0; i < Frames; i++){
            for(unsigned int j = 0; j < ChannelCount; j++, BlocksOffset += BlockSize){
                Writer.SetBuffer(AdxData+BlocksOffset, BlockSize);
                Channels[j].Encode(Writer, BlockSize, SamplesPerBlock, Coefficients, Wavefmt, BitDepth, EncodingMode, Filter);
            }
        }

        /* EOF Scale */
        memset(AdxData+BlocksOffset, 0, BlockSize);
        WriteShortBE(AdxData+BlocksOffset+0, 0x8001);
        WriteShortBE(AdxData+BlocksOffset+2, BlockSize - 4);
        
        if(own) delete[] PCMData;
        return 0;
    }
    unsigned char* GetADX(PCM &PCMMain, unsigned int BitDepth = 4, unsigned int BlockSize = 0x12, unsigned int EncodingMode = 3, unsigned short HighpassFrequency = 500, unsigned int Filter = 0, unsigned int AdxVersion = 4, bool ForceNoLooping = 0){
        unsigned char *ADXData;
        AdxErrorCode = Encode(PCMMain, ADXData, BitDepth, BlockSize, EncodingMode, HighpassFrequency, Filter, AdxVersion, ForceNoLooping);
        return ADXData;
    }
    void GetWAVE(unsigned char* data, PCM &PCMObject){
        AdxErrorCode = Decode(data, PCMObject);
    }
};

static PyObject* AdxEncode(PyObject* self, PyObject* args){
    unsigned char* infilename;
    Py_ssize_t infilename_size;
    unsigned int blocksize;
    unsigned int bitdepth;
    unsigned int encoding_ver;
    unsigned int highpass_freq;
    unsigned int filter;
    unsigned int adx_ver;
    bool force_no_looping;
    if(!PyArg_ParseTuple(args, "y#IIIIIIp", &infilename, &infilename_size, &bitdepth, &blocksize, &encoding_ver, &highpass_freq, &filter, &adx_ver, &force_no_looping)){
        return NULL;
    }
    ADX adx;
    PCM wav;
    char res = wav.LoadDirect(infilename);
    if(res < 0){
        PyPCMSetError(res);
        return NULL;
    }
    unsigned char *adxdata = adx.GetADX(wav, bitdepth, blocksize, encoding_ver, highpass_freq, filter, adx_ver, force_no_looping);
    if(AdxErrorCode){
        PyAdxSetError(AdxErrorCode);
        return NULL;
    }
    unsigned int len = adx.size;
    return Py_BuildValue("y#", adxdata, len);
}

static PyObject* AdxDecode(PyObject* self, PyObject* args){
    unsigned char* data = (unsigned char *)PyBytes_AsString(args);
    ADX adx;
    PCM wav;
    adx.GetWAVE(data, wav);
    if(AdxErrorCode){
        PyAdxSetError(AdxErrorCode);
        return NULL;
    }
    unsigned char *out = wav.WAVEBuffer;
    unsigned int len = wav.wav.size+8;
    return Py_BuildValue("y#", out, len);
}