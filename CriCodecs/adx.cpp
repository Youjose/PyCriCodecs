#define PY_SSIZE_T_CLEAN
#pragma once
#include <Python.h>
#include <iostream>
#include <cstring>

/**
 * ### DECODING ###
 * Shamaless ripoff from Nyagamon/bnnm CRID code, and also improvised from VGMStream decoding method.
 * Although still only works for Encoding version 2, 3 and 4. Encoding 11 and 10 is for AHX.
 * Although I do have to note these codes do not work for any bitdepths other than 4.
 * @todo Support for AINF or CINF information decoding.
 * @todo Adapt for any bitdepths.
 * @todo Decoding looping information.
 * 
 * ### ENCODING ###
 * An uncomplete and modified port of https://github.com/Isaac-Lozano/radx code, 
 * although changed drastically to two functions only. However, the port is uncomplete
 * as I want to add looping information encoding as well.
 * Only works for Encoding version 3.
 * @todo Adapt for any bitdepths.
 * @todo Add encoding for looping information.
 * @todo Add support for Encoding versions 2 and 4.
 */

struct AdxHeader{
    unsigned short signature;
    unsigned short dataoffset;
    unsigned char encoding;
    unsigned char blocksize;
    unsigned char bitdepth;
    unsigned char channelcount;
    unsigned int samplerate;
    unsigned int samplecount;
    unsigned short highpassfrequency;
    unsigned char adxversion;
    unsigned char flags;
    // No looping support yet.
};

struct WAVEHeader{
    char riff[4];
    unsigned int riffSize;
    char wave[4];
    char fmt[4];
    unsigned int fmtSize;
    unsigned short fmtType;
    unsigned short fmtChannelCount;
    unsigned int fmtSamplingRate;
    unsigned int fmtSamplesPerSec;
    unsigned short fmtSamplingSize;
    unsigned short fmtBitCount;
    char data[4];
    unsigned int dataSize;
};

char getbits(int sample, int scale, int *hist, int i){
    int delta = ((sample << 12) - 7400 * hist[i*4] - hist[i*4+1] * -3342) >> 12;
    int unclip;
    if(scale==0)scale=1;
    unclip = (delta>0) ? delta + (scale >> 1) : delta - (scale >> 1);unclip /= scale;
    if(unclip > 7)unclip=7;
    else if(unclip < -8)unclip=-8;
    int bits = unclip;
    int sim_unclip = ((bits << 12) * scale + 7400 * hist[i*4] + hist[i*4+1] * -3342) >> 12;
    if(sim_unclip > 0x7FFF)sim_unclip=0x7FFF;
    else if(sim_unclip < -0x8000)sim_unclip=-0x8000;
    int sim_clip = sim_unclip;
    hist[i*4+1] = hist[i*4];
    hist[i*4] = sim_clip;
    return (char)bits;
}

bool Decode(int *d,unsigned char *s, AdxHeader header){
    if (s[0] == 0x80 && s[1] == 0x01){return false;} // 0x8001 EoF scale.
    char predictor = s[0] >> 5;
	int scale=_byteswap_ushort(*(unsigned short *)s);s+=2;  
    int coeffs[2];
    if(header.encoding == 4){
        scale = 1 << (12 - scale);
        coeffs[0] = 7400;
        coeffs[1] = -3342;
    }else if(header.encoding == 2){
        scale = (scale & 0x1fff) + 1;
        static const int static_coeffs[8] = {0x0000,0x0000,0x0F00,0x0000,0x1CC0,0xF300,0x1880,0xF240};
        coeffs[0] = static_coeffs[predictor*2 + 0];
        coeffs[1] = static_coeffs[predictor*2 + 1];
    }else{
        scale += 1; // Not sure why, but present in VGMStream code.
        coeffs[0] = 7400;
        coeffs[1] = -3342;
    }
	int v,p=d[((header.blocksize-2)*header.channelcount)-1],pp=d[((header.blocksize-2)*header.channelcount)-2];
	for(int i=(header.blocksize-2);i>0;i--,s++){
		v=*s>>4;if(v&8)v-=16;
		v=(v*scale+(coeffs[0] * p >> 12)+(coeffs[1] * pp >> 12));
		pp=p;p=v;*(d++)=v;
		v=*s&0xF;if(v&8)v-=16;
		v=(v*scale+(coeffs[0] * p >> 12)+(coeffs[1] * pp >> 12));
		pp=p;p=v;*(d++)=v;
	}
    return true;
}

bool Decode(void *data,int size,int* _data, AdxHeader &header, char *&outdata){
    for(unsigned char *s=(unsigned char *)data,*e=s+size-header.blocksize*header.channelcount;s<=e;){
        int *d=_data;
        for(unsigned int i=header.channelcount;i>0;i--,d+=((header.blocksize-2)*header.channelcount),s+=header.blocksize){
            if(!Decode(d,s, header)){
                return false;
            }
        }
        d=_data;
        for(int i=((header.blocksize-2)*header.channelcount);i>0&&header.samplecount;i--,d++,header.samplecount--){
            for(unsigned int j=0;j<header.channelcount;j++){
                int v=d[j*((header.blocksize-2)*header.channelcount)];
                if(v>0x7FFF)v=0x7FFF;
                else if(v<-0x8000)v=-0x8000;
                *(unsigned short*)outdata = v;
                outdata+=2;
            }
        }
    }
	return true;
}

void Decode(char* fp, AdxHeader header, char *outdata){
    unsigned int size=header.blocksize*header.channelcount;
	unsigned char *data=new unsigned char [size];
    int *_data = new int [((header.blocksize-2)*header.channelcount)*header.channelcount];
	memset(_data,0,sizeof(int)*((header.blocksize-2)*header.channelcount)*header.channelcount);
    while(header.samplecount){
        for(unsigned int i = 0; i<size; i++, fp++, data++){
            *data = *fp;
        }
        data -= size;
		if (!Decode(data,size,_data, header, outdata)){
            break;
        }
	}
    delete[] data;
    delete[] _data;
}


static PyObject* AdxEncode(PyObject* self, PyObject* args){
    char* infilename;
    Py_ssize_t infilename_size;
    unsigned int blocksize;
    if(!PyArg_ParseTuple(args, "y#I", &infilename, &infilename_size, &blocksize)){
        return NULL;
    }
    WAVEHeader header = *(WAVEHeader *)infilename;
    short *data = new short [header.dataSize/2];
    memcpy(data, infilename, header.dataSize);
    int v, min=0, max=0, scale;
    short *d = data;
    unsigned int len = (blocksize*header.fmtChannelCount)*((header.dataSize/header.fmtSamplingSize)/((blocksize-2)*header.fmtChannelCount));
    char *outbuf = new char [len];
    memset(outbuf,0,len);
    char *out = outbuf;
    int *hist = new int [4*header.fmtChannelCount];
    /**
     * p=i*4, pp=i*4+1, op=i*4+2, opp=i*4+3
     * i: channelcount
     * p: previous
     * o: original
     */
    memset(hist,0,sizeof(int)*4*header.fmtChannelCount);
    short *samples = new short [((blocksize-2)*header.fmtChannelCount)];
    memset(samples,0,sizeof(short)*((blocksize-2)*header.fmtChannelCount));
    while (header.dataSize > 0){
        for(unsigned int i = 0; i < header.fmtChannelCount; i++){
            min=0;max=0;
            d=data+i;
            for(unsigned int j=0; j<((blocksize-2)*header.fmtChannelCount)&&header.dataSize>0;j++,d+=header.fmtChannelCount,header.dataSize-=2){
                v = ((*d << 12) - hist[i*4] * 7400 - hist[i*4+1] * -3342) >> 12;
                if(v < min)min=v;
                else if(v > max)max=v;
                samples[j] = *d;
                hist[i*4+1]=hist[i*4];
                hist[i*4]=*d;
            }
            if(min==0&&max==0){
                for(unsigned int k=0;k<blocksize;k++){
                    *(outbuf++) = (char)0;
                }
                continue;
            }
            scale = (max/7 > min/8) ? max/7 : min/-8;
            hist[i*4]=hist[i*4+2];hist[i*4+1]=hist[i*4+3];
            unsigned short bscale = _byteswap_ushort(scale);
            *(unsigned short*)outbuf = bscale;
            outbuf+=2;
            for(unsigned int j=0;j<((blocksize-2)*header.fmtChannelCount)/2;j++){
                int sample = (int)samples[j*2];
                int sample_= (int)samples[j*2 + 1];
                char upbits = getbits(sample, scale, hist, i);
                char dnbits = getbits(sample_, scale, hist, i);
                char byte = (upbits << 4) | (dnbits & 0xF);
                *(outbuf++) = byte;
            }
            hist[i*4+2]=hist[i*4];
            hist[i*4+3]=hist[i*4+1];
        }
        data+=((blocksize-2)*header.fmtChannelCount)*header.fmtChannelCount;
    }
    return Py_BuildValue("y#", out, len);
}

static PyObject* AdxDecode(PyObject* self, PyObject* args){
    char* data = (char *)PyBytes_AsString(args);
    AdxHeader header = *(AdxHeader *)data;
    data += _byteswap_ushort((header.dataoffset))+4;
    header.samplecount = _byteswap_ulong(header.samplecount);
    unsigned int len = header.channelcount*2*header.samplecount;
    char *outdata = new char [len];
    memset(outdata,0,len);
    char *out = outdata;
    Decode(data, header, outdata);
    return Py_BuildValue("y#", out, len);
}