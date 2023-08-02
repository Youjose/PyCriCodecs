/**
 * @file IO.cpp
 * @brief Read/Write handler for cross-platform endiannes.
 * Most of what's here is ports from other libraries. VGAudio and VGMStream to name a few. 
 */

/* Includes */
#include "IO.hpp"
#include <algorithm>

void WriteChar(unsigned char* Buffer, unsigned char Value){
    Buffer[0] = Value;
}

void WriteShortLE(unsigned char* Buffer, unsigned short Value){
    Buffer[0] = Value & 0xFF;
    Buffer[1] = Value >> 8;
}

void WriteShortBE(unsigned char* Buffer, unsigned short Value){
    Buffer[0] = Value >> 8;
    Buffer[1] = Value & 0xFF;
}

void WriteIntLE(unsigned char* Buffer, unsigned int Value){
    Buffer[0] = (unsigned char)(Value & 0xFF);
    Buffer[1] = (unsigned char)((Value >> 8) & 0xFF);
    Buffer[2] = (unsigned char)((Value >> 16) & 0xFF);
    Buffer[3] = (unsigned char)((Value >> 24) & 0xFF);
}

void WriteIntBE(unsigned char* Buffer, unsigned int Value){
    Buffer[0] = (unsigned char)((Value >> 24) & 0xFF);
    Buffer[1] = (unsigned char)((Value >> 16) & 0xFF);
    Buffer[2] = (unsigned char)((Value >> 8) & 0xFF);
    Buffer[3] = (unsigned char)(Value & 0xFF);
}

unsigned long long BitReader::GetBitsRemaining(){
    return Length - Position;
}

void BitReader::SetBuffer(unsigned char* buffer, unsigned int size){
    Buffer = buffer;
    Length = size * 8;
    Position = 0;
}

int BitReader::ReadInt(int BitCount){
    int value = PeekInt(BitCount);
    Position += BitCount;
    return value;
}

int BitReader::ReadSignedInt(int BitCount){
    int value = PeekInt(BitCount);
    Position += BitCount;
    return SignExtend(value, BitCount);
}

template<typename T> T BitReader::SignExtend(T value, int bits){
    int shift = sizeof(T) * 8 - bits;
    return (value << shift) >> shift;
}

void BitReader::AlignPosition(int multiple){
    Position = GetNextMultiple(Position, multiple);
}

int BitReader::PeekInt(int BitCount){
    unsigned long long Remaining = GetBitsRemaining();
    if (BitCount > Remaining){
        if (Position >= Length) return 0;
        int ExtraBits = BitCount - Remaining;
        return PeekIntFallback(Remaining) << ExtraBits;
    }
    int ByteIndex = Position / 8;
    int BitIndex = Position % 8;
    if (BitCount <= 9 && Remaining >= 16){
        int value = Buffer[ByteIndex] << 8 | Buffer[ByteIndex + 1];
        value &= 0xFFFF >> BitIndex;
        value >>= 16 - BitCount - BitIndex;
        return value;
    }
    if (BitCount <= 17 && Remaining >= 24){
        int value = Buffer[ByteIndex] << 16 | Buffer[ByteIndex + 1] << 8 | Buffer[ByteIndex + 2];
        value &= 0xFFFFFF >> BitIndex;
        value >>= 24 - BitCount - BitIndex;
        return value;
    }
    if (BitCount <= 25 && Remaining >= 32){
        int value = Buffer[ByteIndex] << 24 | Buffer[ByteIndex + 1] << 16 | Buffer[ByteIndex + 2] << 8 | Buffer[ByteIndex + 3];
        value &= (int)(0xFFFFFFFF >> BitIndex);
        value >>= 32 - BitCount - BitIndex;
        return value;
    }
    return PeekIntFallback(BitCount);
}

int BitReader::PeekIntFallback(int BitCount){
    int value = 0;
    int ByteIndex = Position / 8;
    int BitIndex = Position % 8;
    while (BitCount > 0){
        if (BitIndex >= 8){
            BitIndex = 0;
            ByteIndex++;
        }
        int bitsToRead = std::min(BitCount, 8 - BitIndex);
        int mask = 0xFF >> BitIndex;
        int currentByte = (mask & Buffer[ByteIndex]) >> (8 - BitIndex - bitsToRead);
        value = (value << bitsToRead) | currentByte;
        BitIndex += bitsToRead;
        BitCount -= bitsToRead;
    }
    return value;
}

void BitWriter::SetBuffer(unsigned char* buffer, unsigned int size){
    Buffer = buffer;
    Length = size * 8;
    Position = 0;
}

unsigned long long BitWriter::GetBitsRemaining(){
    return Length - Position;
}

void BitWriter::Write(int value, int BitCount){
    unsigned long long Remaining = GetBitsRemaining();
    if(BitCount < 0 || BitCount > 32)
        return;
    else if(BitCount > Remaining)
        return;
    int ByteIndex = Position / 8;
    int BitIndex = Position % 8;
    if (BitCount <= 9 && GetBitsRemaining() >= 16){
        unsigned int outValue = ((value << (16 - BitCount)) & 0xFFFF) >> BitIndex;
        Buffer[ByteIndex] |= (unsigned char)(outValue >> 8);
        Buffer[ByteIndex + 1] = (unsigned char)outValue;
    }else if (BitCount <= 17 && GetBitsRemaining() >= 24){
        int outValue = ((value << (24 - BitCount)) & 0xFFFFFF) >> BitIndex;
        Buffer[ByteIndex] |= (unsigned char)(outValue >> 16);
        Buffer[ByteIndex + 1] = (unsigned char)(outValue >> 8);
        Buffer[ByteIndex + 2] = (unsigned char)outValue;
    }else if (BitCount <= 25 && GetBitsRemaining() >= 32){
        unsigned int outValue = (unsigned int)(((value << (32 - BitCount)) & 0xFFFFFFFF) >> BitIndex);
        Buffer[ByteIndex] |= (unsigned char)(outValue >> 24);
        Buffer[ByteIndex + 1] = (unsigned char)(outValue >> 16);
        Buffer[ByteIndex + 2] = (unsigned char)(outValue >> 8);
        Buffer[ByteIndex + 3] = (unsigned char)outValue;
    }else{
        WriteFallback(value, BitCount);
    }
    Position += BitCount;
}

void BitWriter::AlignPosition(int multiple){
    int newPosition = GetNextMultiple(Position, multiple);
    int bits = newPosition - Position;
    Write(0, bits);
}

void BitWriter::WriteFallback(int value, int BitCount){
    int ByteIndex = Position / 8;
    int BitIndex = Position % 8;
    while(BitCount > 0){
        if(BitIndex >= 8){
            BitIndex = 0;
            ByteIndex++;
        }
        int toShift = 8 - BitIndex - BitCount;
        int shifted = toShift < 0 ? value >> -toShift : value << toShift;
        int bitsToWrite = std::min(BitCount, 8 - BitIndex);
        int mask = ((1 << bitsToWrite) - 1) << 8 - BitIndex - bitsToWrite;
        int outByte = Buffer[ByteIndex] & ~mask;
        outByte |= shifted & mask;
        Buffer[ByteIndex] = (unsigned char)outByte;
        BitIndex += bitsToWrite;
        BitCount -= bitsToWrite;
    }
}