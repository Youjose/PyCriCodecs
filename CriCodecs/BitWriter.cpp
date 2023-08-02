#pragma once
#include <iostream>

class BitWriter{
public:
    unsigned char* buffer;
    unsigned long long len; // This would give maximum file buffer to be 512 megabytes (0xFFFFFFFF * 8).
    unsigned int pos;

    int get_remaining(){
        return len - pos;
    }

    void Write(int value, int bitCount)
    {
        // Debug.Assert(bitCount >= 0 && bitCount <= 32);
        if(bitCount < 0){
            // errors here
            return;
        }else if(bitCount > 32){
            // errors here
            return;
        }

        if (bitCount > get_remaining())
        {
            // errors here. Not enough bits left in the buffer.
            std::cout << "error" << std::endl;
        }

        int byteIndex = pos / 8;
        int bitIndex = pos % 8;

        if (bitCount <= 9 && get_remaining() >= 16)
        {
            int outValue = ((value << (16 - bitCount)) & 0xFFFF) >> bitIndex;

            buffer[byteIndex] |= (unsigned char)(outValue >> 8);
            buffer[byteIndex + 1] = (unsigned char)outValue;
        }

        else if (bitCount <= 17 && get_remaining() >= 24)
        {
            int outValue = ((value << (24 - bitCount)) & 0xFFFFFF) >> bitIndex;

            buffer[byteIndex] |= (unsigned char)(outValue >> 16);
            buffer[byteIndex + 1] = (unsigned char)(outValue >> 8);
            buffer[byteIndex + 2] = (unsigned char)outValue;
        }

        else if (bitCount <= 25 && get_remaining() >= 32)
        {
            int outValue = (int)(((value << (32 - bitCount)) & 0xFFFFFFFF) >> bitIndex);

            buffer[byteIndex] |= (unsigned char)(outValue >> 24);
            buffer[byteIndex + 1] = (unsigned char)(outValue >> 16);
            buffer[byteIndex + 2] = (unsigned char)(outValue >> 8);
            buffer[byteIndex + 3] = (unsigned char)outValue;
        }
        else
        {
            WriteFallback(value, bitCount);
        }

        pos += bitCount;
    }

    void AlignPosition(int multiple){
        int newPosition = GetNextMultiple(pos, multiple);
        int bits = newPosition - pos;
        Write(0, bits);
    }

private:

    int GetNextMultiple(int value, int multiple)
    {
        if (multiple <= 0)
            return value;

z
    }

    void WriteFallback(int value, int bitCount){
        int byteIndex = pos / 8;
        int bitIndex = pos % 8;

        while (bitCount > 0)
        {
            if (bitIndex >= 8)
            {
                bitIndex = 0;
                byteIndex++;
            }

            int toShift = 8 - bitIndex - bitCount;
            int shifted = toShift < 0 ? value >> -toShift : value << toShift;
            int bitsToWrite = std::min(bitCount, 8 - bitIndex);

            int mask = ((1 << bitsToWrite) - 1) << 8 - bitIndex - bitsToWrite;
            int outByte = buffer[byteIndex] & ~mask;
            outByte |= shifted & mask;
            buffer[byteIndex] = (unsigned char)outByte;

            bitIndex += bitsToWrite;
            bitCount -= bitsToWrite;
        }
    }
};