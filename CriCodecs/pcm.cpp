/*
 * This file is made to load .wav file of any PCM mode and convert it internally to
 * a standardized PCM16, this means loss of some data if the wav file uses bitdepth higher
 * than 16 bits. The supported bitdepths conversions would be 8, 16, 24, 32 and 32bit and 64bit IEEE float.
 * Big thanks for VaDiM (aka aelurum) for providing the conversion functions!
 * 
 * However this conversion is necessary since PCM16 is used across all Criware encoders.
 * I will leave the other bitdepths modes structures just for reference.
 * 
 * It's good to note, this library will not support parsing other chunk types for now,
 * the main ones are "fmt", "smpl" and "data".
 * 
*/

/* Includes */
#define PY_SSIZE_T_CLEAN
#pragma once
#include <Python.h>
#include <iostream>
#include "IO.cpp"

static const char* PCMErrorStrings[] = {
    "Invalid WAVE file header.", // -1
    "Invalid WAVE file header. Format info is not present.", // -2
    "Unsupported/Unknown WAVE compression mode.", // -3
    "Invalid looping sample info data.", // -4
    "Invalid looping sample info data, Number of loops/loop data is larger than the available size.", // -5
    "Data tag is not present.", // -6
    "Header is not valid.", // -7
    "PCM Bitdepth does not match compression type.", // -8
    "Filesize exceeds 2GB use python to load in with buffer.", // -9
    "Filesize is too low to be viable for loading." // -10
};

void PyPCMSetError(char ErrorCode){
    ErrorCode = std::abs(ErrorCode);
    PyErr_SetString(PyExc_ValueError, PCMErrorStrings[ErrorCode - 1]);
}

// Some of those might be wrong, the format is not clear.

#define RIFF 0x46464952
#define WAVE 0x45564157
#define FMT  0x20746D66
#define DATA 0x61746164
#define CUE  0x20657563
#define PLST 0x746E6C73
#define LIST 0x746E696C
#define _lst 0x5453494C // Upper case.
#define ADTL 0x6C746461
#define LABL 0x6C62616C
#define LTXT 0x7478746C
#define NOTE 0x65746F6E
#define SMPL 0x6C706D73
#define INST 0x7478746C
#define FACT 0x74636166
#define WAVL 0x6C766177
#define SLNT 0x746E6C73

#define WAVE_FORMAT_PCM        0x0001
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE
#define WAVE_FORMAT_IEEE_FLOAT 0x0003

#define SPEAKER_FRONT_LEFT            0x00000001
#define SPEAKER_FRONT_RIGHT           0x00000002
#define SPEAKER_FRONT_CENTER          0x00000004
#define SPEAKER_LOW_FREQUENCY         0x00000008
#define SPEAKER_BACK_LEFT             0x00000010
#define SPEAKER_BACK_RIGHT            0x00000020
#define SPEAKER_FRONT_LEFT_OF_CENTER  0x00000040
#define SPEAKER_FRONT_RIGHT_OF_CENTER 0x00000080
#define SPEAKER_BACK_CENTER           0x00000100
#define SPEAKER_SIDE_LEFT             0x00000200
#define SPEAKER_SIDE_RIGHT            0x00000400
#define SPEAKER_TOP_CENTER            0x00000800
#define SPEAKER_TOP_FRONT_LEFT        0x00001000
#define SPEAKER_TOP_FRONT_CENTER      0x00002000
#define SPEAKER_TOP_FRONT_RIGHT       0x00004000
#define SPEAKER_TOP_BACK_LEFT         0x00008000
#define SPEAKER_TOP_BACK_CENTER       0x00010000
#define SPEAKER_TOP_BACK_RIGHT        0x00020000
#define SPEAKER_RESERVED              0x80000000

/* 24 bit integer struct, taken from https://stackoverflow.com/a/2682737, 
 * it was made for the same reason, audio manipulation.
 * I added some missing operators to it, and it can be made better in general.
*/
struct __int24{
protected:
    unsigned char m_Internal[3];
public:
    __int24(){}
    __int24( const int val ){*this = val;}
    __int24( const __int24& val ){*this = val;}
    operator int() const{
        if ( m_Internal[2] & 0x80 )
          return (0xff << 24) | (m_Internal[2] << 16) | (m_Internal[1] << 8) | (m_Internal[0] << 0);
        else
          return (m_Internal[2] << 16) | (m_Internal[1] << 8) | (m_Internal[0] << 0);
    }
    operator float() const{return (float)this->operator int();}
    __int24& operator =( const __int24& input ){
      m_Internal[0] = input.m_Internal[0];
      m_Internal[1] = input.m_Internal[1];
      m_Internal[2] = input.m_Internal[2];
      return *this;
    }
    __int24& operator =( const int input ){
      m_Internal[0] = ((unsigned char*)&input)[0];
      m_Internal[1] = ((unsigned char*)&input)[1];
      m_Internal[2] = ((unsigned char*)&input)[2];
      return *this;
    }
    operator bool() const{return (int)*this != 0;}
    __int24  operator +( const __int24& val ) const{return __int24( (int)*this + (int)val );}
    __int24  operator &( const __int24& val ) const{return __int24( (int)*this & (int)val );}
    __int24  operator %( const __int24& val ) const{return __int24( (int)*this % (int)val );}
    __int24  operator -( const __int24& val ) const{return __int24( (int)*this - (int)val );}
    __int24  operator *( const __int24& val ) const{return __int24( (int)*this * (int)val );}
    __int24  operator /( const __int24& val ) const{return __int24( (int)*this / (int)val );}
    __int24  operator +( const int val ) const{return __int24( (int)*this + val );}
    __int24  operator &( const int val ) const{return __int24( (int)*this & val );}
    __int24  operator %( const int val ) const{return __int24( (int)*this % val );}
    __int24  operator -( const int val ) const{return __int24( (int)*this - val );}
    __int24  operator *( const int val ) const{return __int24( (int)*this * val );}
    __int24  operator /( const int val ) const{return __int24( (int)*this / val );}
    __int24  operator <<( const int val ) const{return __int24( (int)*this << val );}
    __int24  operator >>( const int val ) const{return __int24( (int)*this >> val );}
    __int24  operator -(){return __int24( -(int)*this );}
    __int24& operator +=( const __int24& val ){*this = *this + val;return *this;}
    __int24& operator -=( const __int24& val ){*this = *this - val;return *this;}
    __int24& operator *=( const __int24& val ){*this = *this * val;return *this;}
    __int24& operator /=( const __int24& val ){*this = *this / val;return *this;}
    __int24& operator +=( const int val ){*this = *this + val;return *this;}
    __int24& operator -=( const int val ){*this = *this - val;return *this;}
    __int24& operator *=( const int val ){*this = *this * val;return *this;}
    __int24& operator /=( const int val ){*this = *this / val;return *this;}
    __int24& operator >>=( const int val ){*this = *this >> val;return *this;}
    __int24& operator <<=( const int val ){*this = *this << val;return *this;}
    bool     operator !() const{return !((int)*this);}
    bool     operator ==( const __int24& val ) const{return (int)*this == (int)val;}
    bool     operator !=( const __int24& val ) const{return (int)*this != (int)val;}
    bool     operator >=( const __int24& val ) const{return (int)*this >= (int)val;}
    bool     operator <=( const __int24& val ) const{return (int)*this <= (int)val;}
    bool     operator >( const __int24& val ) const{return (int)*this > (int)val;}
    bool     operator <( const __int24& val ) const{return (int)*this < (int)val;}
    bool     operator ==( const int val ) const{return (int)*this == val;}
    bool     operator !=( const int val ) const{return (int)*this != val;}
    bool     operator >=( const int val ) const{return (int)*this >= val;}
    bool     operator <=( const int val ) const{return (int)*this <= val;}
    bool     operator >( const int val ) const{return ((int)*this) > val;}
    bool     operator <( const int val ) const{return (int)*this < val;}
};

template <typename T> T Clamp(T Value, T Limit) {
    if(Value > Limit)
        return Limit;
    else if(Value < ~Limit)
        return ~Limit;
    return Value;
}

struct fmt{
    unsigned int fmt;
    unsigned int size;
    unsigned short CompressionMode;
    unsigned short Channels;
    unsigned int SampleRate;
    unsigned int AverageSamplesPerSecond;
    unsigned short BlockAlign;
    unsigned short BitDepth;
    unsigned short ExtensionSize;
    unsigned short ValidBitsPerSample;
    unsigned int ChannelMask;
    GUID SubFormat;
    inline char loadFMT(unsigned char *data){
        fmt                     = ReadUnsignedIntLE(data+0);
        size                    = ReadUnsignedIntLE(data+4);
        if(size < 16 || fmt != FMT)
            return -2;
        CompressionMode         = ReadUnsignedShortLE(data+8);
        Channels                = ReadUnsignedShortLE(data+10);
        SampleRate              = ReadUnsignedIntLE(data+12);
        AverageSamplesPerSecond = ReadUnsignedIntLE(data+16);
        BlockAlign              = ReadUnsignedShortLE(data+20);
        BitDepth                = ReadUnsignedShortLE(data+22);
        if(size > 16+2 /* ExtensionSize can be 0 */ && CompressionMode == WAVE_FORMAT_EXTENSIBLE){
            ExtensionSize       = ReadUnsignedShortLE(data+24);
            ValidBitsPerSample  = ReadUnsignedShortLE(data+26);
            ChannelMask         = ReadUnsignedIntLE(data+28);
            SubFormat.loadGUID(data+32);
            if(SubFormat.guid1 != WAVE_FORMAT_PCM && SubFormat.guid1 != WAVE_FORMAT_EXTENSIBLE && SubFormat.guid1 != WAVE_FORMAT_IEEE_FLOAT)
                return -3;
        }
        if(CompressionMode != WAVE_FORMAT_PCM && CompressionMode != WAVE_FORMAT_EXTENSIBLE && CompressionMode != WAVE_FORMAT_IEEE_FLOAT)
            return -3;
        return 0;
    }
};

struct smplloop{
    unsigned int CuePointID;
    unsigned int Type;
    unsigned int Start;
    unsigned int End;
    unsigned int Fraction;
    unsigned int PlayCount;
    inline void loadLoop(unsigned char *data){
        CuePointID = ReadUnsignedIntLE(data+0 );
        Type       = ReadUnsignedIntLE(data+4 );
        Start      = ReadUnsignedIntLE(data+8 );
        End        = ReadUnsignedIntLE(data+12);
        Fraction   = ReadUnsignedIntLE(data+16);
        PlayCount  = ReadUnsignedIntLE(data+20);
    }
};

struct smpl{
    unsigned int smpl_magic;
    unsigned int size;
    unsigned int Manufacturer;
    unsigned int Product;
    unsigned int SamplePeriod;
    unsigned int MIDIUnityNote;
    unsigned int MIDIPitchFraction;
    unsigned int SMPTEFormat;
    unsigned int SMPTEOffset;
    unsigned int NumberofSampleLoops;
    unsigned int SamplerDataSize;
    unsigned char *SamplerData;
    smplloop *Loops;
    smpl(){
        Loops = NULL;
    }
    ~smpl(){
        if(Loops != NULL)
            delete[] Loops;
    }
    char loadSMPL(unsigned char *data){
        smpl_magic          = ReadUnsignedIntLE(data+0 );
        size                = ReadUnsignedIntLE(data+4 );
        if(size < 36 || smpl_magic != SMPL)
            return -4;
        Manufacturer        = ReadUnsignedIntLE(data+8 );
        Product             = ReadUnsignedIntLE(data+12);
        SamplePeriod        = ReadUnsignedIntLE(data+16);
        MIDIUnityNote       = ReadUnsignedIntLE(data+20);
        MIDIPitchFraction   = ReadUnsignedIntLE(data+24);
        SMPTEFormat         = ReadUnsignedIntLE(data+28);
        SMPTEOffset         = ReadUnsignedIntLE(data+32);
        NumberofSampleLoops = ReadUnsignedIntLE(data+36);
        SamplerDataSize     = ReadUnsignedIntLE(data+40);
        if(size < NumberofSampleLoops*sizeof(smplloop)+SamplerDataSize+36)
            return -5;
        Loops = new smplloop[NumberofSampleLoops];
        for(unsigned int i = 0, BaseOffset = 44; i < NumberofSampleLoops; i++, BaseOffset+=sizeof(smplloop))
            Loops[i].loadLoop(data+BaseOffset);
        if(SamplerDataSize > 0)
            SamplerData = data+44+NumberofSampleLoops*sizeof(smplloop);
        return 0;
    }
    inline void WriteSMPL(unsigned char *data){
        WriteIntLE(data+0, SMPL);
        WriteIntLE(data+4, 0x3C);
        memset(data+8, 0, 0x3C);
        WriteIntLE(data+0x24, 1); // 1 Loop
        WriteIntLE(data+0x34, Loops[0].Start);
        WriteIntLE(data+0x38, Loops[0].End);
    }
};

struct data{
    unsigned int data;
    unsigned int size;
    unsigned char *DataBuffer;
    char loadDATA(unsigned char *data){
        data::data = ReadUnsignedIntLE(data+0);
        size       = ReadUnsignedIntLE(data+4);
        if(size > 0xFFFFFFFF || data::data != DATA)
            return -6;
        DataBuffer =  data+8;
        return 0;
    }
};

struct WAVEchunks{
    fmt  WAVEfmt;
    smpl WAVEsmpl;
    data WAVEdata;
    bool Looping = 0;
    char loadWAVE(unsigned char *RIFFdata, unsigned int fullsize){
        unsigned int sum_size = 4;
        char res;
        while(sum_size < fullsize){
            unsigned int sig  = ReadUnsignedIntLE(RIFFdata  );
            unsigned int size = ReadUnsignedIntLE(RIFFdata+4)+8;
            size += (size&1 && size+sum_size+(size&1)<=fullsize); // Padding.
            switch(sig){
            case FMT:
                res = WAVEfmt.loadFMT(RIFFdata);
                if(res < 0)
                    return res;
                RIFFdata += size;
                break;
            case SMPL:
                res = WAVEsmpl.loadSMPL(RIFFdata);
                if(res < 0)
                    return res;
                Looping = 1;
                RIFFdata += size;
                break;
            case DATA:
                res = WAVEdata.loadDATA(RIFFdata);
                if(res < 0)
                    return res;
                RIFFdata += size;
                break;
            default:
                RIFFdata += size;
                break;
            }
            sum_size += size;
            if(sum_size > fullsize)
                return -7;
        }
        return 0;
    }
};

struct riff{
    unsigned int riff;
    unsigned int size;
    unsigned int wave; // Named WAVE only for .wav
    WAVEchunks chunks;
    char loadRIFF(unsigned char *data){
        riff = ReadUnsignedIntLE(data+0);
        size = ReadUnsignedIntLE(data+4);
        wave = ReadUnsignedIntLE(data+8);
        if(wave != WAVE || riff != RIFF)
            return -1;
        return chunks.loadWAVE(data+12, size);
    }
    /**
     * @brief Writes Standard WAVE header into a given buffer.
     * 
     * @param data unsigned char* to the buffer you want to write into.
     * @param Channels Number of channels given the PCM.
     * @param SampleRate Sampling Rate.
     */
    inline void writeRIFF(unsigned char* data, unsigned int Channels, unsigned int SampleRate){
        riff = RIFF;
        wave = WAVE;
        const unsigned int SampleSize = 0x02;
        unsigned int Position = 36;
        WriteIntLE(data+0 , riff);
        WriteIntLE(data+4 , size);
        WriteIntLE(data+8 , wave);
        WriteIntLE(data+12,  FMT);
        WriteIntLE(data+16, 0x10);                               // Standard PCM size.
        WriteShortLE(data+20, WAVE_FORMAT_PCM);                  // Standard PCM type.
        WriteShortLE(data+22, Channels);
        WriteIntLE(data+24, SampleRate);
        WriteIntLE(data+28, SampleSize * Channels * SampleRate); // AverageSamplesPerSecond
        WriteShortLE(data+32, SampleSize * Channels);            // Block Align.
        WriteShortLE(data+34, 0x10);                             // BitDepth
        if(chunks.Looping){
            chunks.WAVEsmpl.WriteSMPL(data+36);
            Position = 104;
        }
        WriteIntLE(data+Position, DATA);
        WriteIntLE(data+Position+4, chunks.WAVEdata.size);
        chunks.WAVEfmt.loadFMT(data+12);
        chunks.WAVEdata.data = DATA;
        chunks.WAVEdata.DataBuffer = data + Position + 8;
    }
};

struct PCM{
    unsigned char *WAVEBuffer;  /* Wave buffer.  */
    unsigned char *PCM_8;       /* 1  -> 8  bits */
    short         *PCM_16;      /* 9  -> 16 bits */
    __int24       *PCM_24;      /* 17 -> 24 bits */
    int           *PCM_32;      /* 25 -> 32 bits */
    float         *PCM_IEEE_32; /* 32 float bits */
    double        *PCM_IEEE_64; /* 64 float bits */
    unsigned int ColumnSize;
    bool converted;
    riff wav;

    PCM(){
        WAVEBuffer = NULL;
        PCM_8 = NULL;
        PCM_16 = NULL;
        PCM_24 = NULL;
        PCM_32 = NULL;
        PCM_IEEE_32 = NULL;
        PCM_IEEE_64 = NULL;
        wav = {};
        converted = false;
    }

    ~PCM(){
        if(WAVEBuffer != NULL)
            delete[] WAVEBuffer;
        if(PCM_16 != NULL && converted)
            delete[] PCM_16;
    }

    char load(){
        wav = {};
        char res = wav.loadRIFF(WAVEBuffer);
        if(res < 0)
            return res;
        return load_WAVE(wav.chunks.WAVEfmt, wav.chunks.WAVEdata);
    }

    char load_WAVE(fmt &WAVEfmt, data &WAVEdata){
        unsigned int BitDepth = WAVEfmt.CompressionMode == WAVE_FORMAT_EXTENSIBLE ? WAVEfmt.ValidBitsPerSample : WAVEfmt.BitDepth;
        unsigned int CompressionMode = WAVEfmt.CompressionMode == WAVE_FORMAT_EXTENSIBLE ? WAVEfmt.SubFormat.guid1 : WAVEfmt.CompressionMode;
        unsigned int SampleSize = WAVEfmt.BlockAlign / WAVEfmt.Channels;
        ColumnSize = WAVEdata.size / SampleSize;
        if(CompressionMode == WAVE_FORMAT_IEEE_FLOAT){
            if(BitDepth != 0x20 && BitDepth != 0x40)
                return -8;
            else if(BitDepth == 0x20)
                PCM_IEEE_32 = (float  *)WAVEdata.DataBuffer;
            else
                PCM_IEEE_64 = (double *)WAVEdata.DataBuffer;
        }else{ /* WAVE_FORMAT_PCM */
            if(BitDepth < 1 || BitDepth > 0x20 || SampleSize > 4 || SampleSize < 1)
                return -8;
            else if(BitDepth <= 8  && SampleSize == 1)
                PCM_8 =             WAVEdata.DataBuffer;
            else if(BitDepth <= 16 && SampleSize == 2)
                PCM_16 = (short   *)WAVEdata.DataBuffer;
            else if(BitDepth <= 24 && SampleSize == 3)
                PCM_24 = (__int24 *)WAVEdata.DataBuffer;
            else if(BitDepth <= 32 && SampleSize == 4)
                PCM_32 = (int     *)WAVEdata.DataBuffer;
        }
        return 0;
    }

    /**
     * @brief Generic converter of Float to Integer type PCM data, can have an arbitrary BitDepth as long it doesn't exceed 32.
     * 
     * @tparam T Double or Float.
     * @tparam A Any PCM Bit Depth data type: Unsigned Char, Short, In24, Int.
     * @param PCMSource Source Array of Float PCM data.
     * @param PCMTarget Target Array of Integer PCM Data.
     * @param TargetBitDepth Bit Depth of the wanted PCM data type.
     */
    template <typename T, typename A> inline void Float_to_PCM(T *&PCMSource, A *&PCMTarget, unsigned int TargetBitDepth){
        unsigned int MidPoint = TargetBitDepth <= 8 ? (1 << TargetBitDepth) - 1 : ((unsigned long long)1 << (TargetBitDepth - 1)) - 1;
        for(unsigned int i = 0; i < ColumnSize; i++){
            int value = PCMSource[i] * MidPoint;
            PCMTarget[i] = TargetBitDepth <= 8 ? (value < 0 ? 0 : Clamp<int>(value, MidPoint)) : Clamp<int>(value, MidPoint);
        }
    }

    /**
     * @brief Generic converter from Integer PCM data to Float PCM data.
     * 
     * @tparam T Any PCM Bit Depth data type: Unsigned Char, Short, Int24, Int.
     * @tparam A Double or Float.
     * @param PCMSource Source Array of Integer PCM data.
     * @param PCMTarget Target Array of Float PCM data.
     * @param BitDepth Bit Depth of the source PCM data.
     */
    template <typename T, typename A> inline void PCM_to_Float(T *&PCMSource, A *&PCMTarget, unsigned int BitDepth){
        unsigned int MidPoint = BitDepth <= 8 ? (1 << BitDepth) - 1 : ((unsigned long long)1 << (BitDepth - 1)) - 1;
        for(unsigned int i = 0; i < ColumnSize; i++){
            A value = (A)(BitDepth <= 8 ? PCMSource[i] - MidPoint : PCMSource[i]) / MidPoint;
            PCMTarget[i] = value;
        }
    }

    /**
     * @brief Generic converter of PCM data of bit depths higher than 8bits back to 8bits.
     * 
     * @tparam T Any PCM Bit Depth data type higher than 8 bits: Short, In24, Int.
     * @param PCMSource Source Array of Integer PCM data.
     * @param PCMTarget Target Array of Unsigned Char PCM data.
     * @param BitDepth Bit Depth of the source PCM data.
     */
    template <typename T> inline void PCM_to_PCM8(T *&PCMSource, unsigned char *&PCMTarget, unsigned int BitDepth){
        if(BitDepth <= 8)
            return;
        const unsigned int MidPoint = 0x80; // (1 << (8 - 1))
        unsigned int SampleSizeInBits = BitDepth - 8;
        for(unsigned int i = 0; i < ColumnSize; i++)
            PCMTarget[i] = ((PCMSource[i] >> SampleSizeInBits) ^ MidPoint) & 0xFF;
    }

    /**
     * @brief Generic converter of PCM data of bit depths higher than 16bits back to 16bits.
     * 
     * @tparam T Any PCM Bit Depth data type higher than 16 bits: Short, In24, Int.
     * @param PCMSource Source Array of Integer PCM data.
     * @param PCMTarget Source Array of Short PCM data.
     * @param BitDepth Bit Depth of the source PCM data.
     */
    template <typename T> inline void PCM_to_PCM16(T *&PCMSource, short *&PCMTarget, unsigned int BitDepth){
        if(BitDepth <= 16)
            return;
        unsigned int SampleSizeInBits = BitDepth - 16;
        for(unsigned int i = 0; i < ColumnSize; i++)
            PCMTarget[i] = ((int)PCMSource[i] >> SampleSizeInBits) & 0xFFFF;
    }

    /**
     * @brief Generic converter of PCM data of 8bits to 16bits.
     * 
     * @param BitDepth Bit Depth of the source PCM data.
     */
    void PCM8_to_PCM16(unsigned int BitDepth){
        unsigned int MidPoint = (1 << (BitDepth - 1));
        const unsigned int SampleSizeInBits = 16 - 8; // 8 bit PCM or lower must be in one byte, at least in WAVE.
        for(unsigned int i = 0; i < ColumnSize; i++)
            PCM_16[i] = (PCM_8[i] - MidPoint) << SampleSizeInBits;
    }

    /**
     * @brief Returns PCM data of 16bits Bit Depth, converts to it if needed.
     * 
     * @return short* Array of PCM data.
     */
    short* Get_PCM16(){
        unsigned int BitDepth = wav.chunks.WAVEfmt.CompressionMode == WAVE_FORMAT_EXTENSIBLE ? wav.chunks.WAVEfmt.ValidBitsPerSample : wav.chunks.WAVEfmt.BitDepth;
        unsigned int SampleSize = wav.chunks.WAVEfmt.BlockAlign / wav.chunks.WAVEfmt.Channels;
        if(BitDepth > 8 && BitDepth <= 16 && SampleSize == 2)
            return PCM_16;
        unsigned int CompressionMode = wav.chunks.WAVEfmt.CompressionMode == WAVE_FORMAT_EXTENSIBLE ? wav.chunks.WAVEfmt.SubFormat.guid1 : wav.chunks.WAVEfmt.CompressionMode;
        PCM_16 = new short[ColumnSize];
        converted = true;
        if(BitDepth <= 8)
            PCM8_to_PCM16(BitDepth);
        else if(CompressionMode == WAVE_FORMAT_IEEE_FLOAT)
            BitDepth == 0x20 ? Float_to_PCM(PCM_IEEE_32, PCM_16, 16) : Float_to_PCM(PCM_IEEE_64, PCM_16, 16);
        else
            BitDepth == 0x20 ? PCM_to_PCM16(PCM_32, PCM_16, BitDepth) : PCM_to_PCM16(PCM_24, PCM_16, BitDepth);
        return PCM_16;
    }

    unsigned char* GetWaveBuffer(unsigned int SampleCount, unsigned int Channels, unsigned int SampleRate, bool Looping){
        unsigned int header_size = Looping ? 0x70 : 0x2C;
        wav.size = header_size + SampleCount * Channels * sizeof(short);
        WAVEBuffer = new unsigned char[wav.size];
        wav.size -= 8;
        PCM_16 = (short *)(WAVEBuffer+header_size);
        wav.writeRIFF(WAVEBuffer, Channels, SampleRate);
        return WAVEBuffer;
    }

    /* This function should not be used for files over 2GB in size. */
    char LoadFromFile(const char* Filename){
        FILE* fp = fopen(Filename, "rb");
        fseek(fp, 0, 2);
        long long size = ftell(fp);
        if(size <= 0)
            return -9;
        else if(size < 45) // Assuming 8bit Mono PCM.
            return -10;
        fseek(fp, 0, 0);
        WAVEBuffer = new unsigned char[size];
        fread(WAVEBuffer, size, 1, fp);
        fclose(fp);
        return load();
    }

    /* the given buffer will be called by delete[] after the object distructs. */
    char LoadDirect(unsigned char* Buffer){
        WAVEBuffer = Buffer;
        return load();
    }
};