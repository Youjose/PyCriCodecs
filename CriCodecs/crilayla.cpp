/* 
    CRI layla decompression.
	written by tpu. (https://forum.xentax.com/viewtopic.php?f=21&t=5137&p=44220&hilit=CRILAYLA#p44220)

	Python wrapper by me (and modification).
	Note: I have no idea how this decompression technique works, by the looks of it, it really is
	compute expensive and rather inefficient. If this failes for any kind of file, report to me and I will try
	to rewrite this code. 
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

	return dst;
}

PyObject* CriLaylaDecompress(PyObject* self, PyObject* d){
	unsigned char *data = (unsigned char *)PyBytes_AsString(d);
	crilayla_header header = *(crilayla_header*)data;
	unsigned char *out = layla_decomp((data+16), header);
	return Py_BuildValue("y#", out, header.decompress_size+256);
}