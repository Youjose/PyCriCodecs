/* 
    CRI layla decompression.
	written by tpu. (https://forum.xentax.com/viewtopic.php?f=21&t=5137&p=44220&hilit=CRILAYLA#p44220)
	Python wrapper by me (and modification).

	CRIcompress method by KenTse
    Taken from wmltogether's fork of CriPakTools.
    Python wrapper by me.

	Note: I have no idea how this compression technique works. I just made the wrapper.
*/
#define PY_SSIZE_T_CLEAN
#pragma once
#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct crilayla_header{
    unsigned long long crilayla;
    unsigned int decompress_size;
    unsigned int compressed_size;
};


unsigned char *sbuf;
unsigned int bitcnt;
unsigned int bitdat;
unsigned int get_bits(unsigned int n){
	unsigned int data, mask;

    if (bitcnt<n){
	  data = ((24-bitcnt)>>3)+1;
	  bitcnt += data*8;
      while(data) {
		bitdat = (bitdat<<8) | (*sbuf--);
		data--;
      }
    }

	data = bitdat>>(bitcnt-n);
	bitcnt -= n;
	mask = (1<<n)-1;
	data &= mask;
	return data;
}

unsigned int llcp_dec(unsigned char *src, unsigned int src_len, unsigned char *dst, unsigned int dst_len){
	unsigned char *dbuf, *pbuf;
	unsigned int plen, poffset, byte;

	sbuf = src+src_len-1;
	dbuf = dst+dst_len-1;
	bitcnt = 0;

	while(1){
		if(get_bits(1)==0){
			byte = get_bits(8);
			*dbuf-- = byte;
			if((dbuf+1)==dst)
				goto _done;
		}else{
			poffset = get_bits(13);

			plen = get_bits(2);
			if(plen==3){
				plen += get_bits(3);
				if(plen==10){
					plen += get_bits(5);
					if(plen==41){
						do{
							byte = get_bits(8);
							plen += byte;
						}while(byte==255);
					}
				}
			}

			pbuf = dbuf+poffset+3;
			plen += 3;

			while(plen) {
				byte = *pbuf--;
				*dbuf-- = byte;
				plen--;
				if((dbuf+1)==dst)
					goto _done;
			}

		}
	}

_done:
	return (unsigned int)(dst+dst_len-dbuf-1);
}


unsigned char* layla_decomp(unsigned char* data, crilayla_header header){
    unsigned char *src = new unsigned char[header.compressed_size+256];
    memcpy(src, data, header.compressed_size+256);
	unsigned char tbuf[256];
    unsigned char *dst = new unsigned char[header.decompress_size+256];
    memset(dst,0,header.decompress_size+256);
	memcpy(tbuf, (src+header.compressed_size), 256);
	memcpy(dst, tbuf, 256);
	llcp_dec(src, header.compressed_size, dst+256, header.decompress_size);
    delete[] src;
	return dst;
}

unsigned int layla_comp(unsigned char* dest, unsigned int* destLen, unsigned char* src, unsigned int srcLen){
    unsigned int n = srcLen - 1, m = *destLen - 0x1, T = 0, d = 0, p, q, i, j, k;
    unsigned char* odest = dest;
    for (; n >= 0x100;)
    {
        j = n + 3 + 0x2000;
        if (j > srcLen) j = srcLen;
        for (i = n + 3, p = 0; i < j; i++)
        {
            for (k = 0; k <= n - 0x100; k++)
            {
                if (*(src + n - k) != *(src + i - k)) break;
            }
            if (k > p)
            {
                q = i - n - 3; p = k;
            }
        }
        if (p < 3)
        {
            d = (d << 9) | (*(src + n--)); T += 9;
        }
        else
        {
            d = (((d << 1) | 1) << 13) | q; T += 14; n -= p;
            if (p < 6)
            {
                d = (d << 2) | (p - 3); T += 2;
            }
            else if (p < 13)
            {
                d = (((d << 2) | 3) << 3) | (p - 6); T += 5;
            }
            else if (p < 44)
            {
                d = (((d << 5) | 0x1f) << 5) | (p - 13); T += 10;
            }
            else
            {
                d = ((d << 10) | 0x3ff); T += 10; p -= 44;
                for (;;)
                {
                    for (; T >= 8;)
                    {
                        *(dest + m--) = (d >> (T - 8)) & 0xff; T -= 8; d = d & ((1 << T) - 1);
                    }
                    if (p < 255) break;
                    d = (d << 8) | 0xff; T += 8; p = p - 0xff;
                }
                d = (d << 8) | p; T += 8;
            }
        }
        for (; T >= 8;)
        {
            *(dest + m--) = (d >> (T - 8)) & 0xff; T -= 8; d = d & ((1 << T) - 1);
        }
    }
    if (T != 0)
    {
        *(dest + m--) = d << (8 - T);
    }
    *(dest + m--) = 0; *(dest + m) = 0;
    for (;;)
    {
        if (((*destLen - m) & 3) == 0) break;
        *(dest + m--) = 0;
    }
    *destLen = *destLen - m; dest += m;
    unsigned int l[] = { 0x4c495243,0x414c5941,srcLen - 0x100,*destLen };
    for (j = 0; j < 4; j++)
    {
        for (i = 0; i < 4; i++)
        {
            *(odest + i + j * 4) = l[j] & 0xff; l[j] >>= 8;
        }
    }
    for (j = 0, odest += 0x10; j < *destLen; j++)
    {
        *(odest++) = *(dest + j);
    }
    for (j = 0; j < 0x100; j++)
    {
        *(odest++) = *(src + j);
    }
    *destLen += 0x110;
    return *destLen;
}

PyObject* CriLaylaDecompress(PyObject* self, PyObject* d){
	unsigned char *data = (unsigned char *)PyBytes_AsString(d);
	crilayla_header header = *(crilayla_header*)data;
	unsigned char *out = layla_decomp((data+16), header);
    PyObject *outObj = Py_BuildValue("y#", out, header.decompress_size+256);
    delete[] out;
	return outObj;
}

unsigned char* CriLaylaDecompress(unsigned char* d){
	crilayla_header header = *(crilayla_header*)d;
	unsigned char *out = layla_decomp((d+16), header);
	return out;
}

PyObject* CriLaylaCompress(PyObject* self, PyObject* args){
	unsigned char *data;
	unsigned int data_size;
    if(!PyArg_ParseTuple(args, "y#", &data, &data_size)){
        return NULL;
    }
    unsigned char *buf = new unsigned char[data_size];
    memset(buf, 0, data_size);
    layla_comp(buf, &data_size, data, data_size);
	PyObject* bufObj = Py_BuildValue("y#", buf, data_size);
    delete[] buf;
    return bufObj;
}