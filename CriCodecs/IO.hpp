static inline short ReadShortBE(const unsigned char* ptr){
    return (ptr[0] << 8) | ptr[1];
}
static inline short ReadShortLE(const unsigned char* ptr){
    return ptr[0] | (ptr[1] << 8);
}
static inline int ReadIntBE(const unsigned char* ptr){
    return (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
}
static inline int ReadIntLE(const unsigned char* ptr){
    return ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
}
static inline long long ReadLongLongBE(const unsigned char* ptr) {
    return (unsigned long long)(((unsigned long long)ptr[0] << 56) | ((unsigned long long)ptr[1] << 48) | ((unsigned long long)ptr[2] << 40) | ((unsigned long long)ptr[3] << 32) | ((unsigned long long)ptr[4] << 24) | ((unsigned long long)ptr[5] << 16) | ((unsigned long long)ptr[6] << 8) | (unsigned long long)ptr[7]);
}
static inline long long ReadLongLongLE(const unsigned char* ptr) {
    return (unsigned long long)((unsigned long long)ptr[0] | ((unsigned long long)ptr[1] << 8) | ((unsigned long long)ptr[2] << 16) | ((unsigned long long)ptr[3] << 24) | ((unsigned long long)ptr[4] << 32) | ((unsigned long long)ptr[5] << 40) | ((unsigned long long)ptr[6] << 48) | ((unsigned long long)ptr[7] << 56));
}
static inline unsigned short ReadUnsignedShortBE(const unsigned char* ptr){return (unsigned short)ReadShortBE(ptr);}
static inline unsigned short ReadUnsignedShortLE(const unsigned char* ptr){return (unsigned short)ReadShortLE(ptr);}
static inline unsigned int ReadUnsignedIntBE(const unsigned char* ptr){return (unsigned int)ReadIntBE(ptr);}
static inline unsigned int ReadUnsignedIntLE(const unsigned char* ptr){return (unsigned int)ReadIntLE(ptr);}
static inline unsigned long long ReadUnsignedLongLongBE(const unsigned char* ptr){return (unsigned long long)ReadLongLongBE(ptr);}
static inline unsigned long long ReadUnsignedLongLongLE(const unsigned char* ptr){return (unsigned long long)ReadLongLongLE(ptr);}

static inline int GetNextMultiple(int value, int multiple){
    if (multiple <= 0)
        return value;
    if (value % multiple == 0)
        return value;
    return value + multiple - value % multiple;
}

void WriteChar(unsigned char* Buffer, unsigned char Value);
void WriteShortLE(unsigned char* Buffer, unsigned short Value);
void WriteShortBE(unsigned char* Buffer, unsigned short Value);
void WriteIntLE(unsigned char* Buffer, unsigned int Value);
void WriteIntBE(unsigned char* Buffer, unsigned int Value);

struct BitReader{
    unsigned char* Buffer;
    unsigned long long Length;
    unsigned int Position;

    unsigned long long GetBitsRemaining();
    void SetBuffer(unsigned char* buffer, unsigned int size);
    int ReadInt(int BitCount);
    int ReadSignedInt(int BitCount);
    template<typename T> T SignExtend(T value, int bits);
    void AlignPosition(int multiple);
    int PeekInt(int BitCount);
    int PeekIntFallback(int BitCount);
};

struct BitWriter{
    unsigned char* Buffer;
    unsigned long long Length;
    unsigned int Position;
    void SetBuffer(unsigned char *buffer, unsigned int size);
    unsigned long long GetBitsRemaining();
    void Write(int value, int BitCount);
    void AlignPosition(int multiple);
    void WriteFallback(int value, int BitCount);
};

struct GUID{
    unsigned int guid1;
    unsigned short guid2;
    unsigned short guid3;
    unsigned long long guid4;
    inline void loadGUID(const unsigned char *data){
        guid1 = ReadUnsignedIntLE(data+0);
        guid2 = ReadUnsignedShortLE(data+4);
        guid3 = ReadUnsignedShortLE(data+6);
        guid4 = ReadUnsignedLongLongLE(data+8);
    }
};