#define PY_SSIZE_T_CLEAN
#pragma once
#include <Python.h>
#include <iostream>
#include <cstring>
#include <cmath>
#define M_PI 3.141592653589793

/**
 * ### DECODING ###
 * Shamaless ripoff from Nyagamon/bnnm CRID code, and also improvised from VGMStream decoding method.
 * Although still only works for Encoding version 2, 3 and 4. Encoding 11 and 10 is for AHX.
 * Although I do have to note these codes do not work for any bitdepths other than 4.
 * @todo Add support for encrypted decoding.
 * @todo Support for AINF or CINF information decoding.
 * @todo Adapt for any bitdepths.
 * 
 * ### ENCODING ###
 * A drastically modified port of https://github.com/Isaac-Lozano/radx code,
 * Only works for Encoding version 3.
 * @todo Add support for encrypted encoding.
 * @todo Adapt for any bitdepths.
 * @todo Add support for Encoding versions 2.
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
};

struct Loop{
    unsigned short AlignmentSamples;
    unsigned short LoopCount; // Should be 1, otherwise this is unsupported.
    unsigned short LoopNum;
    unsigned short LoopType;
    unsigned int LoopStartSample;
    unsigned int LoopStartByte;
    unsigned int LoopEndSample;
    unsigned int LoopEndByte;
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
};

struct stWAVEnoteAdx {
    char note[4];
    unsigned int noteSize;
    unsigned int dwName;
};

struct stWAVEdataAdx {
    char data[4];
    unsigned int dataSize;
};

int MultiplyDeBruijnBitPosition[] = {
    0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
    8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31
};

int log2_mod_from_VGAudio(int value){
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;

    return MultiplyDeBruijnBitPosition[(unsigned int)(value * 0x07C4ACDDU) >> 27];
}

int *CalculateCoefficients(int* coefs, unsigned int highpassFreq, int sampleRate)
{
    double sqrt2 = sqrt(2);
    double a = sqrt2 - cos(2.0 * M_PI * highpassFreq / sampleRate);
    double b = sqrt2 - 1;
    double c = (a - sqrt((a + b) * (a - b))) / b;

    coefs[0] = (int)(c * 8192);
    coefs[1] = (int)(c * c * -4096);

    return coefs;
}

char getbits(int sample, int scale, int *hist, int i, int* coeffs){
    int delta = ((sample << 12) - coeffs[0] * hist[i*4] - hist[i*4+1] * coeffs[1]) >> 12;
    // int delta = (sample) - (hist[i*4] * 7400 >> 12) - (hist[i*4+1] * -3342 >> 12);
    int unclip;
    if(scale==0)scale=1;
    unclip = (delta>0) ? delta + (scale >> 1) : delta - (scale >> 1);unclip /= scale;
    if(unclip > 7)unclip=7;
    else if(unclip < -8)unclip=-8;
    int bits = unclip;
    int sim_unclip = ((bits << 12) * scale + coeffs[0] * hist[i*4] + hist[i*4+1] * coeffs[1]) >> 12;
    // int sim_unclip = (bits * scale) + (hist[i*4] * 7400 >> 12) + (hist[i*4+1] * -3342 >> 12);
    if(sim_unclip > 0x7FFF)sim_unclip=0x7FFF;
    else if(sim_unclip < -0x8000)sim_unclip=-0x8000;
    int sim_clip = sim_unclip;
    hist[i*4+1] = hist[i*4];
    hist[i*4] = sim_clip;
    return (char)bits;
}

bool Decode(int *d,unsigned char *s, AdxHeader header, int* coeffs){
    if (s[0] == 0x80 && s[1] == 0x01){return false;} // 0x8001 EoF scale.
    char predictor = s[0] >> 5;
	int scale=_byteswap_ushort(*(unsigned short *)s);s+=2;
    if(header.encoding == 4){
        scale = 1 << (12 - scale);
    }else if(header.encoding == 2){
        scale = (scale & 0x1fff) + 1;
        static const signed short static_coeffs[8] = {0x0000,0x0000,0x0F00,0x0000,0x1CC0,0xF300,0x1880,0xF240};
        coeffs[0] = static_coeffs[predictor*2 + 0];
        coeffs[1] = static_coeffs[predictor*2 + 1];
    }else{
        scale += 1;
    }
	int v,p=d[((header.blocksize-2)*2)-1],pp=d[((header.blocksize-2)*2)-2];
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

bool Decode(void *data,int size,int* _data, AdxHeader &header, char *&outdata, int* coeffs){
    for(unsigned char *s=(unsigned char *)data,*e=s+size-header.blocksize*header.channelcount;s<=e;){
        int *d=_data;
        for(unsigned int i=header.channelcount;i>0;i--,d+=((header.blocksize-2)*2),s+=header.blocksize){
            if(!Decode(d,s, header, coeffs)){
                return false;
            }
        }
        d=_data;
        for(int i=((header.blocksize-2)*2);i>0&&header.samplecount;i--,d++,header.samplecount--){
            for(unsigned int j=0;j<header.channelcount;j++){
                int v=d[j*((header.blocksize-2)*2)]; // 2 samples per byte. This will be the case if the bitdepth is only 4.
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
    int *_data = new int [((header.blocksize-2)*2)*header.channelcount];
	memset(_data,0,sizeof(int)*((header.blocksize-2)*2)*header.channelcount);
    int *coeffs = new int[2];
    coeffs = CalculateCoefficients(coeffs, header.highpassfrequency, header.samplerate);
    while(header.samplecount){
        for(unsigned int i = 0; i<size; i++, fp++, data++){
            *data = *fp;
        }
        data -= size;
		if (!Decode(data,size,_data, header, outdata, coeffs)){
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
    unsigned int encoding_ver;
    unsigned int highpass_freq;
    unsigned int filter;
    if(!PyArg_ParseTuple(args, "y#IIII", &infilename, &infilename_size, &blocksize, &encoding_ver, &highpass_freq, &filter)){
        return NULL;
    }
    WAVEHeader header = *(WAVEHeader *)infilename;
    infilename+=sizeof(WAVEHeader);

    // smpl
    if(*infilename == 0x73 && *(infilename + 1) == 0x6D && *(infilename + 2) == 0x70 && *(infilename + 3) == 0x6C){
        infilename += 0x3C + 8; // Initial check is done via python side.
    }
    // 6E 6F 74 65 = note
    if(*infilename == 0x6E && *(infilename + 1) == 0x6F && *(infilename + 2) == 0x74 && *(infilename + 3) == 0x65){
        stWAVEnoteAdx notechk = *(stWAVEnoteAdx*)infilename;infilename+=sizeof(stWAVEnoteAdx);
        infilename += notechk.noteSize; // +1? also +padding_size?
    }
    // 64 61 74 61 = data
    stWAVEdataAdx datachk;
    if(*infilename == 0x64 && *(infilename + 1) == 0x61 && *(infilename + 2) == 0x74 && *(infilename + 3) == 0x61){
        datachk = *(stWAVEdataAdx*)infilename;infilename+=sizeof(stWAVEdataAdx);
    }
    short *data = new short [datachk.dataSize/2];
    memcpy(data, infilename, datachk.dataSize);
    int v, min=0, max=0, scale, power;
    short *d = data;
    unsigned int len = (blocksize*header.fmtChannelCount)*((datachk.dataSize/header.fmtSamplingSize)/((blocksize-2)*header.fmtChannelCount)) + (blocksize*header.fmtChannelCount);
    char *outbuf = new char [len];
    unsigned int count_forout = 0;
    memset(outbuf,0,len);
    char *out = outbuf;
    int *hist = new int [4*header.fmtChannelCount];
    short Filter = (short)filter;
    /**
     * p=i*4, pp=i*4+1, op=i*4+2, opp=i*4+3
     * i: channelcount
     * p: previous
     * o: original
     */
    memset(hist,0,sizeof(int)*4*header.fmtChannelCount);
    short *samples = new short [((blocksize-2)*header.fmtChannelCount)];
    memset(samples,0,sizeof(short)*((blocksize-2)*header.fmtChannelCount));
    int* coeffs = new int [2];
    coeffs = CalculateCoefficients(coeffs, highpass_freq, header.fmtSamplingRate);
    if((Filter == 0 || Filter == 1 || Filter == 2 || Filter == 3) && highpass_freq == 0 && encoding_ver == 2){
        static const signed short static_coeffs[8] = {0x0000,0x0000,0x0F00,0x0000,0x1CC0,0xF300,0x1880,0xF240};
        coeffs[0] = static_coeffs[Filter*2 + 0];
        coeffs[1] = static_coeffs[Filter*2 + 1];
    }
    Filter = Filter << 13;
    while (datachk.dataSize > 0){
        for(unsigned int i = 0; i < header.fmtChannelCount; i++){
            min=0;max=0;
            int md = 0;
            int y=0;
            d=data+i;
            for(unsigned int j=0; j<((blocksize-2)*header.fmtChannelCount)&&datachk.dataSize>0;j++,d+=header.fmtChannelCount,datachk.dataSize-=2){
                v = (((int)(*d) << 12) - hist[i*4] * coeffs[0] - hist[i*4+1] * coeffs[1]) >> 12;
                // v = *d - (hist[i*4] * 7400 >> 12) - (hist[i*4+1] * -3342 >> 12);
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
            unsigned short bscale;
            switch (encoding_ver)
            {
            case 4:
                scale = (max/7 > min/-8) ? max/7 : min/-8;
                if(scale > 0x1000)scale=0x1000;
                power = scale == 0 ? 0 : log2_mod_from_VGAudio(scale) + 1;
                scale = 1 << power;
                bscale = _byteswap_ushort(12 - power);
                break;
            case 2:
                scale = (max/7 > min/-8) ? max/7 : min/-8;
                if(scale > 0x1000)scale=0x1000;
                bscale = _byteswap_ushort(Filter | (scale & 0x1FFF));
                break;
            default:
                scale = (max/7 > min/-8) ? max/7 : min/-8;
                if(scale > 0x1000)scale=0x1000;
                bscale = _byteswap_ushort(scale);
                break;
            }

            hist[i*4]=hist[i*4+2];hist[i*4+1]=hist[i*4+3];
            *(unsigned short*)outbuf = bscale;
            outbuf+=2;
            for(unsigned int j=0;j<((blocksize-2)*header.fmtChannelCount)/2;j++){
                int sample = (int)samples[j*2];
                int sample_= (int)samples[j*2 + 1];
                char upbits = getbits(sample, scale, hist, i, coeffs);
                char dnbits = getbits(sample_, scale, hist, i, coeffs);
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
    header.samplerate = _byteswap_ulong(header.samplerate);
    header.highpassfrequency = _byteswap_ushort(header.highpassfrequency);
    unsigned int len = header.channelcount*2*header.samplecount;
    char *outdata = new char [len];
    memset(outdata,0,len);
    char *out = outdata;
    Decode(data, header, outdata);
    return Py_BuildValue("y#", out, len);
}