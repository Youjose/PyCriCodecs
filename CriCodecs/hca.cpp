/**
 * ### Decoding ### (Still WIP)
 * This code is copy and paste from VGMStream. Which in itself is taken from various libraries and improved upon.
 * This decoding code is the work of others and not mine by any capacity, big shoutout for the amazing people who wrote this.
 * CREDITS:
 * #########################################################################################
 * clHCA DECODER
 *
 * Decodes CRI's HCA (High Compression Audio), a CBR DCT-based codec (similar to AAC).
 * Also supports what CRI calls HCA-MX, which basically is the same thing with constrained
 * encoder settings.
 *
 * - Original decompilation and C++ decoder by nyaga
 *     https://github.com/Nyagamon/HCADecoder
 * - Ported to C by kode54
 *     https://gist.github.com/kode54/ce2bf799b445002e125f06ed833903c0
 * - Cleaned up and re-reverse engineered for HCA v3 by bnnm, using Thealexbarney's VGAudio decoder as reference
 *     https://github.com/Thealexbarney/VGAudio
 * - And of course, VGMStream
 *     https://github.com/vgmstream/vgmstream
 * #########################################################################################
 * ### Encoding ###
 * A port of https://github.com/Thealexbarney/VGAudio to C++. Hardest copy and past I've ever did.
 * All credit to VGAudio/TheAlexBarney and VGMStream.
 */

//--------------------------------------------------
// Includes
//--------------------------------------------------
#define PY_SSIZE_T_CLEAN
#pragma once
#include <Python.h>
#include "hca.h"
#include "BitWriter.cpp"
#include <stddef.h>
#include <stdlib.h>
#include <memory.h>
#include <iostream>
#include <cstring>
#include <cmath>

/* CRI libs may only accept last version in some cases/modes, though most decoding takes older versions
 * into account. Lib is identified with "HCA Decoder (Float)" + version string. Some known versions:
 * - ~V1.1 2011 [first public version]
 * - ~V1.2 2011 [ciph/ath chunks, disabled ATH]
 * - Ver.1.40 2011-04 [header mask]
 * - Ver.1.42 2011-05
 * - Ver.1.45.02 2012-03
 * - Ver.2.00.02 2012-06 [decoding updates]
 * - Ver.2.02.02 2013-12, 2014-11
 * - Ver.2.06.05 2018-12 [scramble subkey API]
 * - Ver.2.06.07 2020-02, 2021-02
 * - Ver.3.01.00 2020-11 [decoding updates]
 * Same version rebuilt gets a newer date, and new APIs change header strings, but header versions
 * only change when decoder does. Despite the name, no "Integer" version seems to exist.
 */
#define HCA_VERSION_V101 0x0101 /* V1.1+ [El Shaddai (PS3/X360)] */
#define HCA_VERSION_V102 0x0102 /* V1.2+ [Gekka Ryouran Romance (PSP)] */
#define HCA_VERSION_V103 0x0103 /* V1.4+ [Phantasy Star Online 2 (PC), Binary Domain (PS3)] */
#define HCA_VERSION_V200 0x0200 /* V2.0+ [Yakuza 5 (PS3)] */
#define HCA_VERSION_V300 0x0300 /* V3.0+ [Uma Musume (Android), Megaton Musashi (Switch)-sfx-hfrgroups] */

/* maxs depend on encoder quality settings (for example, stereo has:
 * highest=0x400, high=0x2AA, medium=0x200, low=0x155, lowest=0x100) */
#define HCA_MIN_FRAME_SIZE 0x8          /* lib min */
#define HCA_MAX_FRAME_SIZE 0xFFFF       /* lib max */

#define HCA_MASK  0x7F7F7F7F            /* chunk obfuscation when the HCA is encrypted with key */
#define HCA_SUBFRAMES  8
#define HCA_SAMPLES_PER_SUBFRAME  128   /* also spectrum points/etc */
#define HCA_SAMPLES_PER_FRAME  (HCA_SUBFRAMES*HCA_SAMPLES_PER_SUBFRAME)
#define HCA_MDCT_BITS  7                /* (1<<7) = 128 */

#define HCA_MIN_CHANNELS  1
#define HCA_MAX_CHANNELS  16            /* internal max (in practice only 8 can be encoded) */
#define HCA_MIN_SAMPLE_RATE  1          /* assumed */
#define HCA_MAX_SAMPLE_RATE  0x7FFFFF   /* encoder max seems 48000 */

#define HCA_DEFAULT_RANDOM  1

#define HCA_RESULT_OK            0
#define HCA_ERROR_PARAMS        -1
#define HCA_ERROR_HEADER        -2
#define HCA_ERROR_CHECKSUM      -3
#define HCA_ERROR_SYNC          -4
#define HCA_ERROR_UNPACK        -5
#define HCA_ERROR_BITREADER     -6

//--------------------------------------------------
// Decoder config/state
//--------------------------------------------------
typedef enum { DISCRETE = 0, STEREO_PRIMARY = 1, STEREO_SECONDARY = 2 } channel_type_t;

typedef struct stChannel {
    /* HCA channel config */
    channel_type_t type;
    unsigned int coded_count;                                        /* encoded scales/resolutions/coefs */

    /* subframe state */
    unsigned char intensity[HCA_SUBFRAMES];                          /* intensity indexes for joins stereo (value max: 15 / 4b) */
    unsigned char scalefactors[HCA_SAMPLES_PER_SUBFRAME];            /* scale indexes (value max: 64 / 6b)*/
    unsigned char resolution[HCA_SAMPLES_PER_SUBFRAME];              /* resolution indexes (value max: 15 / 4b) */
    unsigned char noises[HCA_SAMPLES_PER_SUBFRAME];                  /* indexes to coefs that need noise fill + coefs that don't (value max: 128 / 8b) */
    unsigned int noise_count;                                        /* resolutions with noise values saved in 'noises' */
    unsigned int valid_count;                                        /* resolutions with valid values saved in 'noises' */

    float gain[HCA_SAMPLES_PER_SUBFRAME];                            /* gain to apply to quantized spectral data */
    float spectra[HCA_SUBFRAMES][HCA_SAMPLES_PER_SUBFRAME];          /* resulting dequantized data */ // HCA output will go in here.
    
    float scaledspectra[HCA_SAMPLES_PER_SUBFRAME][HCA_SUBFRAMES];    /* Addition to VGMStream channel from VGAudio */
    float QuantizedSpectra[HCA_SUBFRAMES][HCA_SAMPLES_PER_SUBFRAME]; /* Addition to VGMStream channel from VGAudio */
    float HfrGroupAverageSpectra[HCA_SUBFRAMES];                     /* Addition to VGMStream channel from VGAudio */

    float temp[HCA_SAMPLES_PER_SUBFRAME];                            /* temp for DCT-IV */
    float dct[HCA_SAMPLES_PER_SUBFRAME];                             /* result of DCT-IV */
    float imdct_previous[HCA_SAMPLES_PER_SUBFRAME];                  /* IMDCT */ /* I will use this as well for MDCT since it has the same size, can't bother to rename */

    /* Additional stuff from VGAudio. */
    int HfrScales[HCA_SUBFRAMES];
    int HeaderLengthBits;
    int ScaleFactorDeltaBits;

    /* frame state */
    float wave[HCA_SUBFRAMES][HCA_SAMPLES_PER_SUBFRAME];  /* resulting samples */
} stChannel;

typedef struct clHCA {
    /* header config */
    unsigned int is_valid;
    /* hca chunk */
    unsigned int version;
    unsigned int header_size;
    /* fmt chunk */
    unsigned int channels;
    unsigned int sample_rate;
    unsigned int frame_count;
    unsigned int encoder_delay;
    unsigned int encoder_padding;
    /* comp/dec chunk */
    unsigned int frame_size;
    unsigned int min_resolution;
    unsigned int max_resolution;
    unsigned int track_count;
    unsigned int channel_config;
    unsigned int stereo_type;
    unsigned int total_band_count;
    unsigned int base_band_count;
    unsigned int stereo_band_count;
    // Note here, these are in VGMStream code as well, but aren't part of the struct.
    // Also I am not sure if they are unsigned as well, VGAudio has them as "int".
    unsigned int HfrBandCount; /* Added from VGAudio. Used for Encoding only. */
    int AcceptableNoiseLevel;  /* Added from VGAudio. Used for Encoding only. */
    int EvaluationBoundary;    /* Added from VGAudio. Used for Encoding only. */
    unsigned int bands_per_hfr_group;
    unsigned int ms_stereo;
    unsigned int reserved;
    /* vbr chunk */
    unsigned int vbr_max_frame_size;
    unsigned int vbr_noise_Level;
    /* ath chunk */
    unsigned int ath_type;
    /* loop chunk */
    unsigned int loop_start_frame;
    unsigned int loop_end_frame;
    unsigned int loop_start_delay;
    unsigned int loop_end_padding;
    unsigned int loop_flag;
    /* ciph chunk */
    unsigned int ciph_type;
    unsigned long long keycode;
    /* rva chunk */
    float rva_volume;
    /* comm chunk */
    unsigned int comment_len; /* max 0xFF */
    char comment[255+1];

    /* initial state */
    unsigned int hfr_group_count;                       /* high frequency band groups not encoded directly */
    unsigned char ath_curve[HCA_SAMPLES_PER_SUBFRAME];
    unsigned char cipher_table[256];
    /* variable state */
    unsigned int random;
    stChannel channel[HCA_MAX_CHANNELS];
} clHCA;

typedef struct clData {
    const unsigned char* data;
    int size;
    int bit;
} clData;


//--------------------------------------------------
// Checksum
//--------------------------------------------------
static const unsigned short hcacommon_crc_mask_table[256] = {
    0x0000,0x8005,0x800F,0x000A,0x801B,0x001E,0x0014,0x8011,0x8033,0x0036,0x003C,0x8039,0x0028,0x802D,0x8027,0x0022,
    0x8063,0x0066,0x006C,0x8069,0x0078,0x807D,0x8077,0x0072,0x0050,0x8055,0x805F,0x005A,0x804B,0x004E,0x0044,0x8041,
    0x80C3,0x00C6,0x00CC,0x80C9,0x00D8,0x80DD,0x80D7,0x00D2,0x00F0,0x80F5,0x80FF,0x00FA,0x80EB,0x00EE,0x00E4,0x80E1,
    0x00A0,0x80A5,0x80AF,0x00AA,0x80BB,0x00BE,0x00B4,0x80B1,0x8093,0x0096,0x009C,0x8099,0x0088,0x808D,0x8087,0x0082,
    0x8183,0x0186,0x018C,0x8189,0x0198,0x819D,0x8197,0x0192,0x01B0,0x81B5,0x81BF,0x01BA,0x81AB,0x01AE,0x01A4,0x81A1,
    0x01E0,0x81E5,0x81EF,0x01EA,0x81FB,0x01FE,0x01F4,0x81F1,0x81D3,0x01D6,0x01DC,0x81D9,0x01C8,0x81CD,0x81C7,0x01C2,
    0x0140,0x8145,0x814F,0x014A,0x815B,0x015E,0x0154,0x8151,0x8173,0x0176,0x017C,0x8179,0x0168,0x816D,0x8167,0x0162,
    0x8123,0x0126,0x012C,0x8129,0x0138,0x813D,0x8137,0x0132,0x0110,0x8115,0x811F,0x011A,0x810B,0x010E,0x0104,0x8101,
    0x8303,0x0306,0x030C,0x8309,0x0318,0x831D,0x8317,0x0312,0x0330,0x8335,0x833F,0x033A,0x832B,0x032E,0x0324,0x8321,
    0x0360,0x8365,0x836F,0x036A,0x837B,0x037E,0x0374,0x8371,0x8353,0x0356,0x035C,0x8359,0x0348,0x834D,0x8347,0x0342,
    0x03C0,0x83C5,0x83CF,0x03CA,0x83DB,0x03DE,0x03D4,0x83D1,0x83F3,0x03F6,0x03FC,0x83F9,0x03E8,0x83ED,0x83E7,0x03E2,
    0x83A3,0x03A6,0x03AC,0x83A9,0x03B8,0x83BD,0x83B7,0x03B2,0x0390,0x8395,0x839F,0x039A,0x838B,0x038E,0x0384,0x8381,
    0x0280,0x8285,0x828F,0x028A,0x829B,0x029E,0x0294,0x8291,0x82B3,0x02B6,0x02BC,0x82B9,0x02A8,0x82AD,0x82A7,0x02A2,
    0x82E3,0x02E6,0x02EC,0x82E9,0x02F8,0x82FD,0x82F7,0x02F2,0x02D0,0x82D5,0x82DF,0x02DA,0x82CB,0x02CE,0x02C4,0x82C1,
    0x8243,0x0246,0x024C,0x8249,0x0258,0x825D,0x8257,0x0252,0x0270,0x8275,0x827F,0x027A,0x826B,0x026E,0x0264,0x8261,
    0x0220,0x8225,0x822F,0x022A,0x823B,0x023E,0x0234,0x8231,0x8213,0x0216,0x021C,0x8219,0x0208,0x820D,0x8207,0x0202,
};

//HCACommon_CalculateCrc
static unsigned short crc16_checksum(const unsigned char* data, unsigned int size) {
    unsigned int i;
    unsigned short sum = 0;

    /* HCA header/frames should always have checksum 0 (checksum(size-16b) = last 16b) */
    for (i = 0; i < size; i++) {
        sum = (sum << 8) ^ hcacommon_crc_mask_table[(sum >> 8) ^ data[i]];
    }
    return sum;
}

//--------------------------------------------------
// Bitstream reader
//--------------------------------------------------
static void bitreader_init(clData* br, unsigned char *data, int size) {
    br->data = data;
    br->size = size * 8;
    br->bit = 0;
}

/* CRI's bitreader only handles 16b max during decode (header just reads bytes)
 * so maybe could be optimized by ignoring higher cases */
static unsigned int bitreader_peek(clData* br, int bitsize) {
    const unsigned int bit = br->bit;
    const unsigned int bit_rem = bit & 7;
    const unsigned int size = br->size;
    unsigned int v = 0;
    unsigned int bit_offset, bit_left;

    if (!(bit + bitsize <= size))
        return v;

    bit_offset = bitsize + bit_rem;
    bit_left = size - bit;
    if (bit_left >= 32 && bit_offset >= 25) {
        static const unsigned int mask[8] = {
                0xFFFFFFFF,0x7FFFFFFF,0x3FFFFFFF,0x1FFFFFFF,
                0x0FFFFFFF,0x07FFFFFF,0x03FFFFFF,0x01FFFFFF
        };
        const unsigned char* data = &br->data[bit >> 3];
        v = data[0];
        v = (v << 8) | data[1];
        v = (v << 8) | data[2];
        v = (v << 8) | data[3];
        v &= mask[bit_rem];
        v >>= 32 - bit_rem - bitsize;
    }
    else if (bit_left >= 24 && bit_offset >= 17) {
        static const unsigned int mask[8] = {
                0xFFFFFF,0x7FFFFF,0x3FFFFF,0x1FFFFF,
                0x0FFFFF,0x07FFFF,0x03FFFF,0x01FFFF
        };
        const unsigned char* data = &br->data[bit >> 3];
        v = data[0];
        v = (v << 8) | data[1];
        v = (v << 8) | data[2];
        v &= mask[bit_rem];
        v >>= 24 - bit_rem - bitsize;
    }
    else if (bit_left >= 16 && bit_offset >= 9) {
        static const unsigned int mask[8] = {
                0xFFFF,0x7FFF,0x3FFF,0x1FFF,0x0FFF,0x07FF,0x03FF,0x01FF
        };
        const unsigned char* data = &br->data[bit >> 3];
        v = data[0];
        v = (v << 8) | data[1];
        v &= mask[bit_rem];
        v >>= 16 - bit_rem - bitsize;
    }
    else {
        static const unsigned int mask[8] = {
                0xFF,0x7F,0x3F,0x1F,0x0F,0x07,0x03,0x01
        };
        const unsigned char* data = &br->data[bit >> 3];
        v = data[0];
        v &= mask[bit_rem];
        v >>= 8 - bit_rem - bitsize;
    }
    return v;
}

static unsigned int bitreader_read(clData* br, int bitsize) {
    unsigned int v = bitreader_peek(br, bitsize);
    br->bit += bitsize;
    return v;
}

static void bitreader_skip(clData* br, int bitsize) {
    br->bit += bitsize;
}

//--------------------------------------------------
// API/Utilities
//--------------------------------------------------

int clHCA_isOurFile(unsigned char *data, unsigned int size) {
    clData br;
    unsigned int header_size = 0;

    if (!data || size < 0x08)
        return HCA_ERROR_PARAMS;

    bitreader_init(&br, data, 8);
    if ((bitreader_peek(&br, 32) & HCA_MASK) == 0x48434100) {/*'HCA\0'*/
        bitreader_skip(&br, 32 + 16);
        header_size = bitreader_read(&br, 16);
    }

    if (header_size == 0)
        return HCA_ERROR_HEADER;
    return header_size;
}

int clHCA_getInfo(clHCA* hca, clHCA_stInfo *info) {
    if (!hca || !info || !hca->is_valid)
        return HCA_ERROR_PARAMS;

    info->version = hca->version;
    info->headerSize = hca->header_size;
    info->samplingRate = hca->sample_rate;
    info->channelCount = hca->channels;
    info->blockSize = hca->frame_size;
    info->blockCount = hca->frame_count;
    info->encoderDelay = hca->encoder_delay;
    info->encoderPadding = hca->encoder_padding;
    info->loopEnabled = hca->loop_flag;
    info->loopStartBlock = hca->loop_start_frame;
    info->loopEndBlock = hca->loop_end_frame;
    info->loopStartDelay = hca->loop_start_delay;
    info->loopEndPadding = hca->loop_end_padding;
    info->samplesPerBlock = HCA_SAMPLES_PER_FRAME;
    info->comment = hca->comment;
    info->encryptionEnabled = hca->ciph_type == 56; /* keycode encryption */
    return 0;
}

//HCADecoder_DecodeBlockInt32
void clHCA_ReadSamples16(clHCA* hca, signed short *&samples) {
    const float scale_f = 32768.0f;
    float f;
    signed int s;
    unsigned int i, j, k;

    /* PCM output is generally unused, but lib functions seem to use SIMD for f32 to s32 + round to zero */
    for (i = 0; i < HCA_SUBFRAMES; i++) {
        for (j = 0; j < HCA_SAMPLES_PER_SUBFRAME; j++) {
            for (k = 0; k < hca->channels; k++) {
                f = hca->channel[k].wave[i][j];
                //f = f * hca->rva_volume; /* rare, won't apply for now */
                s = (signed int)(f * scale_f);
                if (s > 32767)
                    s = 32767;
                else if (s < -32768)
                    s = -32768;
                *samples++ = (signed short)s;
            }
        }
    }
}


//--------------------------------------------------
// Allocation and creation
//--------------------------------------------------
static void clHCA_constructor(clHCA* hca) {
    if (!hca)
        return;
    memset(hca, 0, sizeof(*hca));
    hca->is_valid = 0;
}

static void clHCA_destructor(clHCA* hca) {
    hca->is_valid = 0;
}

int clHCA_sizeof(void) {
    return sizeof(clHCA);
}

void clHCA_clear(clHCA* hca) {
    clHCA_constructor(hca);
}

void clHCA_done(clHCA* hca) {
    clHCA_destructor(hca);
}

clHCA* clHCA_new(void) {
    clHCA* hca = (clHCA*)malloc(clHCA_sizeof());
    if (hca) {
        clHCA_constructor(hca);
    }
    return hca;
}

void clHCA_delete(clHCA* hca) {
    clHCA_destructor(hca);
    free(hca);
}

//--------------------------------------------------
// ATH
//--------------------------------------------------
/* Base ATH (Absolute Threshold of Hearing) curve (for 41856hz).
 * May be a slight modification of the standard Painter & Spanias ATH curve formula. */
static const unsigned char ath_base_curve[656] = {
    0x78,0x5F,0x56,0x51,0x4E,0x4C,0x4B,0x49,0x48,0x48,0x47,0x46,0x46,0x45,0x45,0x45,
    0x44,0x44,0x44,0x44,0x43,0x43,0x43,0x43,0x43,0x43,0x42,0x42,0x42,0x42,0x42,0x42,
    0x42,0x42,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x40,0x40,0x40,0x40,
    0x40,0x40,0x40,0x40,0x40,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
    0x3F,0x3F,0x3F,0x3E,0x3E,0x3E,0x3E,0x3E,0x3E,0x3D,0x3D,0x3D,0x3D,0x3D,0x3D,0x3D,
    0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,
    0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,
    0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3B,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,
    0x3D,0x3D,0x3D,0x3D,0x3D,0x3D,0x3D,0x3D,0x3E,0x3E,0x3E,0x3E,0x3E,0x3E,0x3E,0x3F,
    0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
    0x3F,0x3F,0x3F,0x3F,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
    0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x41,0x41,0x41,0x41,0x41,0x41,0x41,
    0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,
    0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,
    0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x43,0x43,0x43,
    0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x43,0x44,0x44,
    0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x45,0x45,0x45,0x45,
    0x45,0x45,0x45,0x45,0x45,0x45,0x45,0x45,0x46,0x46,0x46,0x46,0x46,0x46,0x46,0x46,
    0x46,0x46,0x47,0x47,0x47,0x47,0x47,0x47,0x47,0x47,0x47,0x47,0x48,0x48,0x48,0x48,
    0x48,0x48,0x48,0x48,0x49,0x49,0x49,0x49,0x49,0x49,0x49,0x49,0x4A,0x4A,0x4A,0x4A,
    0x4A,0x4A,0x4A,0x4A,0x4B,0x4B,0x4B,0x4B,0x4B,0x4B,0x4B,0x4C,0x4C,0x4C,0x4C,0x4C,
    0x4C,0x4D,0x4D,0x4D,0x4D,0x4D,0x4D,0x4E,0x4E,0x4E,0x4E,0x4E,0x4E,0x4F,0x4F,0x4F,
    0x4F,0x4F,0x4F,0x50,0x50,0x50,0x50,0x50,0x51,0x51,0x51,0x51,0x51,0x52,0x52,0x52,
    0x52,0x52,0x53,0x53,0x53,0x53,0x54,0x54,0x54,0x54,0x54,0x55,0x55,0x55,0x55,0x56,
    0x56,0x56,0x56,0x57,0x57,0x57,0x57,0x57,0x58,0x58,0x58,0x59,0x59,0x59,0x59,0x5A,
    0x5A,0x5A,0x5A,0x5B,0x5B,0x5B,0x5B,0x5C,0x5C,0x5C,0x5D,0x5D,0x5D,0x5D,0x5E,0x5E,
    0x5E,0x5F,0x5F,0x5F,0x60,0x60,0x60,0x61,0x61,0x61,0x61,0x62,0x62,0x62,0x63,0x63,
    0x63,0x64,0x64,0x64,0x65,0x65,0x66,0x66,0x66,0x67,0x67,0x67,0x68,0x68,0x68,0x69,
    0x69,0x6A,0x6A,0x6A,0x6B,0x6B,0x6B,0x6C,0x6C,0x6D,0x6D,0x6D,0x6E,0x6E,0x6F,0x6F,
    0x70,0x70,0x70,0x71,0x71,0x72,0x72,0x73,0x73,0x73,0x74,0x74,0x75,0x75,0x76,0x76,
    0x77,0x77,0x78,0x78,0x78,0x79,0x79,0x7A,0x7A,0x7B,0x7B,0x7C,0x7C,0x7D,0x7D,0x7E,
    0x7E,0x7F,0x7F,0x80,0x80,0x81,0x81,0x82,0x83,0x83,0x84,0x84,0x85,0x85,0x86,0x86,
    0x87,0x88,0x88,0x89,0x89,0x8A,0x8A,0x8B,0x8C,0x8C,0x8D,0x8D,0x8E,0x8F,0x8F,0x90,
    0x90,0x91,0x92,0x92,0x93,0x94,0x94,0x95,0x95,0x96,0x97,0x97,0x98,0x99,0x99,0x9A,
    0x9B,0x9B,0x9C,0x9D,0x9D,0x9E,0x9F,0xA0,0xA0,0xA1,0xA2,0xA2,0xA3,0xA4,0xA5,0xA5,
    0xA6,0xA7,0xA7,0xA8,0xA9,0xAA,0xAA,0xAB,0xAC,0xAD,0xAE,0xAE,0xAF,0xB0,0xB1,0xB1,
    0xB2,0xB3,0xB4,0xB5,0xB6,0xB6,0xB7,0xB8,0xB9,0xBA,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,
    0xC0,0xC1,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xC9,0xCA,0xCB,0xCC,0xCD,
    0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,
    0xDE,0xDF,0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xED,0xEE,
    0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFF,0xFF,
};

static void ath_init0(unsigned char* ath_curve) {
    /* disable curve */
    memset(ath_curve, 0, sizeof(ath_curve[0]) * HCA_SAMPLES_PER_SUBFRAME);
}

static void ath_init1(unsigned char* ath_curve, unsigned int sample_rate) {
    unsigned int i, index;
    unsigned int acc = 0;

    /* scale ATH curve depending on frequency */
    for (i = 0; i < HCA_SAMPLES_PER_SUBFRAME; i++) {
        acc += sample_rate;
        index = acc >> 13;

        if (index >= 654) {
            memset(ath_curve+i, 0xFF, sizeof(ath_curve[0]) * (HCA_SAMPLES_PER_SUBFRAME - i));
            break;
        }
        ath_curve[i] = ath_base_curve[index];
    }
}

static int ath_init(unsigned char* ath_curve, int type, unsigned int sample_rate) {
    switch (type) {
        case 0:
            ath_init0(ath_curve);
            break;
        case 1:
            ath_init1(ath_curve, sample_rate);
            break;
        default:
            return HCA_ERROR_HEADER;
    }
    return HCA_RESULT_OK;
}


//--------------------------------------------------
// Encryption
//--------------------------------------------------
static void cipher_decrypt(unsigned char* cipher_table, unsigned char* data, int size) {
    unsigned int i;

    for (i = 0; i < size; i++) {
        data[i] = cipher_table[data[i]];
    }
}

static void cipher_init0(unsigned char* cipher_table) {
    unsigned int i;

    /* no encryption */
    for (i = 0; i < 256; i++) {
        cipher_table[i] = i;
    }
}

static void cipher_init1(unsigned char* cipher_table) {
    const int mul = 13;
    const int add = 11;
    unsigned int i, v = 0;

    /* keyless encryption (rare) */
    for (i = 1; i < 256 - 1; i++) {
        v = (v * mul + add) & 0xFF;
        if (v == 0 || v == 0xFF)
            v = (v * mul + add) & 0xFF;
        cipher_table[i] = v;
    }
    cipher_table[0] = 0;
    cipher_table[0xFF] = 0xFF;
}

static void cipher_init56_create_table(unsigned char* r, unsigned char key) {
    const int mul = ((key & 1) << 3) | 5;
    const int add = (key & 0xE) | 1;
    unsigned int i;

    key >>= 4;
    for (i = 0; i < 16; i++) {
        key = (key * mul + add) & 0xF;
        r[i] = key;
    }
}

static void cipher_init56(unsigned char* cipher_table, unsigned long long keycode) {
    unsigned char kc[8];
    unsigned char seed[16];
    unsigned char base[256], base_r[16], base_c[16];
    unsigned int r, c;

    /* 56bit keycode encryption (given as a uint64_t number, but upper 8b aren't used) */

    /* keycode = keycode - 1 */
    if (keycode != 0)
        keycode--;

    /* init keycode table */
    for (r = 0; r < (8-1); r++) {
        kc[r] = keycode & 0xFF;
        keycode = keycode >> 8;
    }

    /* init seed table */
    seed[0x00] = kc[1];
    seed[0x01] = kc[1] ^ kc[6];
    seed[0x02] = kc[2] ^ kc[3];
    seed[0x03] = kc[2];
    seed[0x04] = kc[2] ^ kc[1];
    seed[0x05] = kc[3] ^ kc[4];
    seed[0x06] = kc[3];
    seed[0x07] = kc[3] ^ kc[2];
    seed[0x08] = kc[4] ^ kc[5];
    seed[0x09] = kc[4];
    seed[0x0A] = kc[4] ^ kc[3];
    seed[0x0B] = kc[5] ^ kc[6];
    seed[0x0C] = kc[5];
    seed[0x0D] = kc[5] ^ kc[4];
    seed[0x0E] = kc[6] ^ kc[1];
    seed[0x0F] = kc[6];

    /* init base table */
    cipher_init56_create_table(base_r, kc[0]);
    for (r = 0; r < 16; r++) {
        unsigned char nb;
        cipher_init56_create_table(base_c, seed[r]);
        nb = base_r[r] << 4;
        for (c = 0; c < 16; c++) {
            base[r*16 + c] = nb | base_c[c]; /* combine nibbles */
        }
    }

    /* final shuffle table */
    {
        unsigned int i;
        unsigned int x = 0;
        unsigned int pos = 1;

        for (i = 0; i < 256; i++) {
            x = (x + 17) & 0xFF;
            if (base[x] != 0 && base[x] != 0xFF)
                cipher_table[pos++] = base[x];
        }
        cipher_table[0] = 0;
        cipher_table[0xFF] = 0xFF;
    }
}

static int cipher_init(unsigned char* cipher_table, int type, unsigned long long keycode) {
    if (type == 56 && !(keycode))
        type = 0;

    switch (type) {
        case 0:
            cipher_init0(cipher_table);
            break;
        case 1:
            cipher_init1(cipher_table);
            break;
        case 56:
            cipher_init56(cipher_table, keycode);
            break;
        default:
            return HCA_ERROR_HEADER;
    }
    return HCA_RESULT_OK;
}

//--------------------------------------------------
// Parse
//--------------------------------------------------
static unsigned int header_ceil2(unsigned int a, unsigned int b) {
    if (b < 1)
        return 0;
    return (a / b + ((a % b) ? 1 : 0)); /* lib modulo: a - (a / b * b) */
}

int clHCA_DecodeHeader(clHCA* hca, unsigned char *data, unsigned int size) {
    clData br;
    int res;

    if (!hca || !data)
        return HCA_ERROR_PARAMS;

    hca->is_valid = 0;

    if (size < 0x08)
        return HCA_ERROR_PARAMS;

    bitreader_init(&br, data, size);

    /* read header chunks (in HCA chunks must follow a fixed order) */

    /* HCA base header */
    if ((bitreader_peek(&br, 32) & HCA_MASK) == 0x48434100) { /* "HCA\0" */
        bitreader_skip(&br, 32);
        hca->version = bitreader_read(&br, 16); /* lib reads as version + subversion (uses main version for feature checks) */
        hca->header_size = bitreader_read(&br, 16);

        if (hca->version != HCA_VERSION_V101 &&
            hca->version != HCA_VERSION_V102 &&
            hca->version != HCA_VERSION_V103 &&
            hca->version != HCA_VERSION_V200 &&
            hca->version != HCA_VERSION_V300)
            return HCA_ERROR_HEADER;

        if (size < hca->header_size)
            return HCA_ERROR_PARAMS;

        if (crc16_checksum(data,hca->header_size))
            return HCA_ERROR_CHECKSUM;

        size -= 0x08;
    }
    else {
        return HCA_ERROR_HEADER;
    }

    /* format info */
    if (size >= 0x10 && (bitreader_peek(&br, 32) & HCA_MASK) == 0x666D7400) { /* "fmt\0" */
        bitreader_skip(&br, 32);
        hca->channels = bitreader_read(&br, 8);
        hca->sample_rate = bitreader_read(&br, 24);
        hca->frame_count = bitreader_read(&br, 32);
        hca->encoder_delay = bitreader_read(&br, 16);
        hca->encoder_padding = bitreader_read(&br, 16);

        if (!(hca->channels >= HCA_MIN_CHANNELS && hca->channels <= HCA_MAX_CHANNELS))
            return HCA_ERROR_HEADER;

        if (hca->frame_count == 0)
            return HCA_ERROR_HEADER;

        if (!(hca->sample_rate >= HCA_MIN_SAMPLE_RATE && hca->sample_rate <= HCA_MAX_SAMPLE_RATE))
            return HCA_ERROR_HEADER;

        size -= 0x10;
    }
    else {
        return HCA_ERROR_HEADER;
    }

    /* compression (v2.0) or decode (v1.x) info */
    if (size >= 0x10 && (bitreader_peek(&br, 32) & HCA_MASK) == 0x636F6D70) { /* "comp" */
        bitreader_skip(&br, 32);
        hca->frame_size = bitreader_read(&br, 16);
        hca->min_resolution = bitreader_read(&br, 8);
        hca->max_resolution = bitreader_read(&br, 8);
        hca->track_count = bitreader_read(&br, 8);
        hca->channel_config = bitreader_read(&br, 8);
        hca->total_band_count = bitreader_read(&br, 8);
        hca->base_band_count = bitreader_read(&br, 8);
        hca->stereo_band_count = bitreader_read(&br, 8);
        hca->bands_per_hfr_group = bitreader_read(&br, 8);
        hca->ms_stereo = bitreader_read(&br, 8);
        hca->reserved = bitreader_read(&br, 8); /* not actually read by lib */

        size -= 0x10;
    }
    else if (size >= 0x0c && (bitreader_peek(&br, 32) & HCA_MASK) == 0x64656300) { /* "dec\0" */
        bitreader_skip(&br, 32);
        hca->frame_size = bitreader_read(&br, 16);
        hca->min_resolution = bitreader_read(&br, 8);
        hca->max_resolution = bitreader_read(&br, 8);
        hca->total_band_count = bitreader_read(&br, 8) + 1;
        hca->base_band_count = bitreader_read(&br, 8) + 1;
        hca->track_count = bitreader_read(&br, 4);
        hca->channel_config = bitreader_read(&br, 4);
        hca->stereo_type = bitreader_read(&br, 8);

        if (hca->stereo_type == 0)
            hca->base_band_count = hca->total_band_count;
        hca->stereo_band_count = hca->total_band_count - hca->base_band_count;
        hca->bands_per_hfr_group = 0;

        size -= 0x0c;
    }
    else {
        return HCA_ERROR_HEADER;
    }

    /* VBR (variable bit rate) info */
    if (size >= 0x08 && (bitreader_peek(&br, 32) & HCA_MASK) == 0x76627200) { /* "vbr\0" */
        bitreader_skip(&br, 32);
        hca->vbr_max_frame_size = bitreader_read(&br, 16);
        hca->vbr_noise_Level = bitreader_read(&br, 16);

        if (!(hca->frame_size == 0 && hca->vbr_max_frame_size > 8 && hca->vbr_max_frame_size <= 0x1FF))
            return HCA_ERROR_HEADER;

        size -= 0x08;
    }
    else {
        /* removed in v2.0, probably unused in v1.x */
        hca->vbr_max_frame_size = 0;
        hca->vbr_noise_Level = 0;
    }

    /* ATH (Absolute Threshold of Hearing) info */
    if (size >= 0x06 && (bitreader_peek(&br, 32) & HCA_MASK) == 0x61746800) { /* "ath\0" */
        bitreader_skip(&br, 32);
        hca->ath_type = bitreader_read(&br, 16);
    }
    else {
        /* removed in v2.0, default in v1.x (only used in v1.1, as v1.2/v1.3 set ath_type = 0) */
        hca->ath_type = (hca->version < HCA_VERSION_V200) ? 1 : 0;
    }

    /* loop info */
    if (size >= 0x10 && (bitreader_peek(&br, 32) & HCA_MASK) == 0x6C6F6F70) { /* "loop" */
        bitreader_skip(&br, 32);
        hca->loop_start_frame = bitreader_read(&br, 32);
        hca->loop_end_frame = bitreader_read(&br, 32);
        hca->loop_start_delay = bitreader_read(&br, 16);
        hca->loop_end_padding = bitreader_read(&br, 16);

        hca->loop_flag = 1;

        if (!(hca->loop_start_frame >= 0 && hca->loop_start_frame <= hca->loop_end_frame
                && hca->loop_end_frame < hca->frame_count))
            return HCA_ERROR_HEADER;

        size -= 0x10;
    }
    else {
        hca->loop_start_frame = 0;
        hca->loop_end_frame = 0;
        hca->loop_start_delay = 0;
        hca->loop_end_padding = 0;

        hca->loop_flag = 0;
    }

    /* cipher/encryption info */
    if (size >= 0x06 && (bitreader_peek(&br, 32) & HCA_MASK) == 0x63697068) { /* "ciph" */
        bitreader_skip(&br, 32);
        hca->ciph_type = bitreader_read(&br, 16);

        if (!(hca->ciph_type == 0 || hca->ciph_type == 1 || hca->ciph_type == 56))
            return HCA_ERROR_HEADER;

        size -= 0x06;
    }
    else {
        hca->ciph_type = 0;
    }

    /* RVA (relative volume adjustment) info */
    if (size >= 0x08 && (bitreader_peek(&br, 32) & HCA_MASK) == 0x72766100) { /* "rva\0" */
        union {
            unsigned int i;
            float f;
        } rva_volume_cast;
        bitreader_skip(&br, 32);
        rva_volume_cast.i = bitreader_read(&br, 32);
        hca->rva_volume = rva_volume_cast.f;

        size -= 0x08;
    } else {
        hca->rva_volume = 1.0f; /* encoder volume setting is pre-applied to data, though chunk still exists in +v3.0 */
    }

    /* comment */
    if (size >= 0x05 && (bitreader_peek(&br, 32) & HCA_MASK) == 0x636F6D6D) {/* "comm" */
        unsigned int i;
        bitreader_skip(&br, 32);
        hca->comment_len = bitreader_read(&br, 8);

        if (hca->comment_len > size)
            return HCA_ERROR_HEADER;

        for (i = 0; i < hca->comment_len; ++i)
            hca->comment[i] = bitreader_read(&br, 8);
        hca->comment[i] = '\0'; /* should be null terminated but make sure */

        size -= 0x05 + hca->comment_len;
    }
    else {
        hca->comment_len = 0;
    }

    /* padding info */
    if (size >= 0x04 && (bitreader_peek(&br, 32) & HCA_MASK) == 0x70616400) { /* "pad\0" */
        size -= (size - 0x02); /* fills up to header_size, sans checksum */
    }

    /* should be fully read, but allow as data buffer may be bigger than header_size */
    //if (size != 0x02)
    //    return HCA_ERROR_HEADER;


    /* extra validations */
    if (!(hca->frame_size >= HCA_MIN_FRAME_SIZE && hca->frame_size <= HCA_MAX_FRAME_SIZE)) /* actual max seems 0x155*channels */
        return HCA_ERROR_HEADER; /* theoretically can be 0 if VBR (not seen) */

    if (hca->version <= HCA_VERSION_V200) {
        if (hca->min_resolution != 1 || hca->max_resolution != 15)
            return HCA_ERROR_HEADER;
    }
    else {
        if (hca->min_resolution > hca->max_resolution || hca->max_resolution > 15) /* header seems to allow 31, but later max is 15 */
            return HCA_ERROR_HEADER;
    }


    /* init state */

    if (hca->track_count == 0)
        hca->track_count = 1; /* as done by lib, can be 0 in old HCAs */

    if (hca->track_count > hca->channels)
        return HCA_ERROR_HEADER;

    /* encoded coefs (up to 128) depend in the encoder's "cutoff" hz option */
    if (hca->total_band_count > HCA_SAMPLES_PER_SUBFRAME ||
        hca->base_band_count > HCA_SAMPLES_PER_SUBFRAME ||
        hca->stereo_band_count > HCA_SAMPLES_PER_SUBFRAME ||
        hca->base_band_count + hca->stereo_band_count > HCA_SAMPLES_PER_SUBFRAME ||
        hca->bands_per_hfr_group > HCA_SAMPLES_PER_SUBFRAME)
        return HCA_ERROR_HEADER;

    hca->hfr_group_count = header_ceil2(
            hca->total_band_count - hca->base_band_count - hca->stereo_band_count,
            hca->bands_per_hfr_group);

    res = ath_init(hca->ath_curve, hca->ath_type, hca->sample_rate);
    if (res < 0)
        return res;
    res = cipher_init(hca->cipher_table, hca->ciph_type, hca->keycode);
    if (res < 0)
        return res;


    //todo separate into function, cleanup
    //HCAHeaderUtility_GetElementTypes
    /* init channels */
    {
        channel_type_t channel_types[HCA_MAX_CHANNELS] = {(channel_type_t)0}; /* part of lib struct */
        unsigned int i, channels_per_track;

        channels_per_track = hca->channels / hca->track_count;
        if (hca->stereo_band_count > 0 && channels_per_track > 1) {
            channel_type_t* ct = channel_types;
            for (i = 0; i < hca->track_count; i++, ct += channels_per_track) {
                switch (channels_per_track) {
                    case 2:
                        ct[0] = STEREO_PRIMARY;
                        ct[1] = STEREO_SECONDARY;
                        break;
                    case 3:
                        ct[0] = STEREO_PRIMARY;
                        ct[1] = STEREO_SECONDARY;
                        ct[2] = DISCRETE;
                        break;
                    case 4:
                        ct[0] = STEREO_PRIMARY;
                        ct[1] = STEREO_SECONDARY;
                        if (hca->channel_config == 0) {
                            ct[2] = STEREO_PRIMARY;
                            ct[3] = STEREO_SECONDARY;
                        } else {
                            ct[2] = DISCRETE;
                            ct[3] = DISCRETE;
                        }
                        break;
                    case 5:
                        ct[0] = STEREO_PRIMARY;
                        ct[1] = STEREO_SECONDARY;
                        ct[2] = DISCRETE;
                        if (hca->channel_config <= 2) {
                            ct[3] = STEREO_PRIMARY;
                            ct[4] = STEREO_SECONDARY;
                        } else {
                            ct[3] = DISCRETE;
                            ct[4] = DISCRETE;
                        }
                        break;
                    case 6:
                        ct[0] = STEREO_PRIMARY;
                        ct[1] = STEREO_SECONDARY;
                        ct[2] = DISCRETE;
                        ct[3] = DISCRETE;
                        ct[4] = STEREO_PRIMARY;
                        ct[5] = STEREO_SECONDARY;
                        break;
                    case 7:
                        ct[0] = STEREO_PRIMARY;
                        ct[1] = STEREO_SECONDARY;
                        ct[2] = DISCRETE;
                        ct[3] = DISCRETE;
                        ct[4] = STEREO_PRIMARY;
                        ct[5] = STEREO_SECONDARY;
                        ct[6] = DISCRETE;
                        break;
                    case 8:
                        ct[0] = STEREO_PRIMARY;
                        ct[1] = STEREO_SECONDARY;
                        ct[2] = DISCRETE;
                        ct[3] = DISCRETE;
                        ct[4] = STEREO_PRIMARY;
                        ct[5] = STEREO_SECONDARY;
                        ct[6] = STEREO_PRIMARY;
                        ct[7] = STEREO_SECONDARY;
                        break;
                    default:
                        /* implied all 0 (DISCRETE) */
                        break;
                }
            }
        }

        memset(hca->channel, 0, sizeof(hca->channel));
        for (i = 0; i < hca->channels; i++) {
            hca->channel[i].type = channel_types[i];

            hca->channel[i].coded_count = (channel_types[i] != STEREO_SECONDARY) ?
                    hca->base_band_count + hca->stereo_band_count :
                    hca->base_band_count;
        }
    }

    hca->random = HCA_DEFAULT_RANDOM;


    //TODO: should work but untested
    if (hca->ms_stereo)
        return HCA_ERROR_HEADER;

    /* clHCA is correctly initialized and decoder state reset
     * (keycode is not changed between calls) */
    hca->is_valid = 1;

    return HCA_RESULT_OK;
}

void clHCA_SetKey(clHCA* hca, unsigned long long keycode) {
    if (!hca)
        return;
    hca->keycode = keycode;

    /* May be called even if clHCA is not valid (header not parsed), as the
     * key will be used during DecodeHeader ciph init. If header was already
     * parsed reinitializes the decryption table using the new key. */
    if (hca->is_valid) {
        /* ignore error since it can't really fail */
        cipher_init(hca->cipher_table, hca->ciph_type, hca->keycode);
    }
}

static int clHCA_DecodeBlock_unpack(clHCA* hca, unsigned char *data, unsigned int size);
static void clHCA_DecodeBlock_transform(clHCA* hca);


int clHCA_TestBlock(clHCA* hca, unsigned char *data, unsigned int size) {
    const int frame_samples = HCA_SUBFRAMES * HCA_SAMPLES_PER_SUBFRAME;
    const float scale = 32768.0f;
    unsigned int i, ch, sf, s;
    int status;
    int clips = 0, blanks = 0, channel_blanks[HCA_MAX_CHANNELS] = {0};
    const unsigned char* buf = data;

    /* first blocks can be empty/silent, check all bytes but sync/crc */
    {
        int is_empty = 1;

        for (i = 0x02; i < size - 0x02; i++) {
            if (buf[i] != 0) {
                is_empty = 0;
                break;
            }
        }

        if (is_empty) {
            return 0;
        }
    }

    /* return if decode fails (happens often with wrong keys due to bad bitstream values) */
    status = clHCA_DecodeBlock_unpack(hca, data, size);
    if (status < 0)
        return -1;

    /* detect data errors */
    {
        int bits_max = hca->frame_size * 8;
        int byte_start;

        /* Should read all frame sans end checksum (16b), but allow 14b as one frame was found to
         * read up to that (cross referenced with CRI's tools), perhaps some encoding hiccup
         * [World of Final Fantasy Maxima (Switch) am_ev21_0170 video] */
        if (status + 14 > bits_max)
            return HCA_ERROR_BITREADER;

        /* leftover data after read bits in HCA is always null (up to end 16b checksum), so bad keys
         * give garbage beyond those bits (data is decrypted at this point and size >= frame_size) */
        byte_start = (status / 8) + (status % 8 ? 0x01 : 0);
        /* maybe should memcmp with a null frame, unsure of max though, and in most cases
         * should fail fast (this check catches almost everything) */
        for (i = byte_start; i < hca->frame_size - 0x02; i++) {
            if (buf[i] != 0) {
                return -1;
            }
        }
    }

    /* check decode results as (rarely) bad keys may still get here */
    clHCA_DecodeBlock_transform(hca);
    for (ch = 0; ch < hca->channels; ch++) {
        for (sf = 0; sf < HCA_SUBFRAMES; sf++) {
            for (s = 0; s < HCA_SAMPLES_PER_SUBFRAME; s++) {
                float fsample = hca->channel[ch].wave[sf][s];

                if (fsample > 1.0f || fsample < -1.0f) { //improve?
                    clips++;
                }
                else {
                    signed int psample = (signed int) (fsample * scale);
                    if (psample == 0 || psample == -1) {
                        blanks++;
                        channel_blanks[ch]++;
                    }
                }
            }
        }
    }

    /* the more clips the less likely block was correctly decrypted */
    if (clips == 1)
        clips++; /* signal not full score */
    if (clips > 1)
        return clips;

    /* if block is silent result is not useful */
    if (blanks == hca->channels * frame_samples)
        return 0;

    /* some bad keys make left channel null and right normal enough (due to joint stereo stuff);
     * it's possible real keys could do this but don't give full marks just in case */
    if (hca->channels >= 2) {
        /* only check main L/R, other channels like BL/BR are probably not useful */
        if (channel_blanks[0] == frame_samples && channel_blanks[1] != frame_samples) /* maybe should check max/min values? */
            return 3;
    }

    /* block may be correct (but wrong keys can get this too and should test more blocks) */
    return 1;
}

void clHCA_DecodeReset(clHCA * hca) {
    unsigned int i;

    if (!hca || !hca->is_valid)
        return;

    hca->random = HCA_DEFAULT_RANDOM;

    for (i = 0; i < hca->channels; i++) {
        stChannel* ch = &hca->channel[i];

        /* most values get overwritten during decode */
        //memset(ch->intensity, 0, sizeof(ch->intensity[0]) * HCA_SUBFRAMES);
        //memset(ch->scalefactors, 0, sizeof(ch->scalefactors[0]) * HCA_SAMPLES_PER_SUBFRAME);
        //memset(ch->resolution, 0, sizeof(ch->resolution[0]) * HCA_SAMPLES_PER_SUBFRAME);
        //memset(ch->gain, 0, sizeof(ch->gain[0]) * HCA_SAMPLES_PER_SUBFRAME);
        //memset(ch->spectra, 0, sizeof(ch->spectra[0]) * HCA_SUBFRAMES * HCA_SAMPLES_PER_SUBFRAME);
        //memset(ch->temp, 0, sizeof(ch->temp[0]) * HCA_SAMPLES_PER_SUBFRAME);
        //memset(ch->dct, 0, sizeof(ch->dct[0]) * HCA_SAMPLES_PER_SUBFRAME);
        memset(ch->imdct_previous, 0, sizeof(ch->imdct_previous[0]) * HCA_SAMPLES_PER_SUBFRAME);
        //memset(ch->wave, 0, sizeof(ch->wave[0][0]) * HCA_SUBFRAMES * HCA_SUBFRAMES);
    }
}

//--------------------------------------------------
// Decode
//--------------------------------------------------
static int unpack_scalefactors(stChannel* ch, clData* br, unsigned int hfr_group_count, unsigned int version);

static int unpack_intensity(stChannel* ch, clData* br, unsigned int hfr_group_count, unsigned int version);

static void calculate_resolution(stChannel* ch, unsigned int packed_noise_level, const unsigned char* ath_curve,
    unsigned int min_resolution, unsigned int max_resolution);

static void calculate_gain(stChannel* ch);

static void dequantize_coefficients(stChannel* ch, clData* br, int subframe);

static void reconstruct_noise(stChannel* ch, unsigned int min_resolution, unsigned int ms_stereo, unsigned int* random_p, int subframe);

static void reconstruct_high_frequency(stChannel* ch, unsigned int hfr_group_count, unsigned int bands_per_hfr_group,
        unsigned int stereo_band_count, unsigned int base_band_count, unsigned int total_band_count, unsigned int version, int subframe);

static void apply_intensity_stereo(stChannel* ch_pair, int subframe, unsigned int base_band_count, unsigned int total_band_count);

static void apply_ms_stereo(stChannel* ch_pair, unsigned int ms_stereo, unsigned int base_band_count, unsigned int total_band_count, int subframe);

static void imdct_transform(stChannel* ch, int subframe);


static int clHCA_DecodeBlock_unpack(clHCA* hca, unsigned char *data, unsigned int size) {
    clData br;
    unsigned short sync;
    unsigned int subframe, ch;

    if (!data || !hca || !hca->is_valid)
        return HCA_ERROR_PARAMS;
    if (size < hca->frame_size)
        return HCA_ERROR_PARAMS;

    bitreader_init(&br, data, hca->frame_size);

    /* test sync (not encrypted) */
    sync = bitreader_read(&br, 16);
    if (sync != 0xFFFF)
        return HCA_ERROR_SYNC;

    if (crc16_checksum(data, hca->frame_size))
        return HCA_ERROR_CHECKSUM;

    cipher_decrypt(hca->cipher_table, data, hca->frame_size);


    /* unpack frame values */
    {
        /* lib saves this in the struct since they can stop/resume subframe decoding */
        unsigned int frame_acceptable_noise_level = bitreader_read(&br, 9);
        unsigned int frame_evaluation_boundary = bitreader_read(&br, 7);

        unsigned int packed_noise_level = (frame_acceptable_noise_level << 8) - frame_evaluation_boundary;

        for (ch = 0; ch < hca->channels; ch++) {
            int err = unpack_scalefactors(&hca->channel[ch], &br, hca->hfr_group_count, hca->version);
            if (err < 0)
                return err;

            unpack_intensity(&hca->channel[ch], &br, hca->hfr_group_count, hca->version);

            calculate_resolution(&hca->channel[ch], packed_noise_level, hca->ath_curve, hca->min_resolution, hca->max_resolution);

            calculate_gain(&hca->channel[ch]);
        }
    }

    /* lib seems to use a state value to skip parts (unpacking/subframe N/etc) as needed */
    for (subframe = 0; subframe < HCA_SUBFRAMES; subframe++) {

        /* unpack channel data and get dequantized spectra */
        for (ch = 0; ch < hca->channels; ch++){
            dequantize_coefficients(&hca->channel[ch], &br, subframe);
        }

        /* original code transforms subframe here, but we have it for later */
    }

    return br.bit; /* numbers of read bits for validations */
}

static void clHCA_DecodeBlock_transform(clHCA* hca) {
    unsigned int subframe, ch;

    for (subframe = 0; subframe < HCA_SUBFRAMES; subframe++) {
        /* restore missing bands from spectra */
        for (ch = 0; ch < hca->channels; ch++) {
            reconstruct_noise(&hca->channel[ch], hca->min_resolution, hca->ms_stereo, &hca->random, subframe);

            reconstruct_high_frequency(&hca->channel[ch], hca->hfr_group_count, hca->bands_per_hfr_group,
                    hca->stereo_band_count, hca->base_band_count, hca->total_band_count, hca->version, subframe);
        }

        /* restore missing joint stereo bands */
        if (hca->stereo_band_count > 0) {
            for (ch = 0; ch < hca->channels - 1; ch++) {
                apply_intensity_stereo(&hca->channel[ch], subframe, hca->base_band_count, hca->total_band_count);

                apply_ms_stereo(&hca->channel[ch], hca->ms_stereo, hca->base_band_count, hca->total_band_count, subframe);
            }
        }

        /* apply imdct */
        for (ch = 0; ch < hca->channels; ch++) {
            imdct_transform(&hca->channel[ch], subframe);
        }
    }
}


/* takes HCA data and decodes all of a frame's samples */
//hcadecoder_decode_block
int clHCA_DecodeBlock(clHCA* hca, unsigned char *data, unsigned int size) {
    int res;

    /* Original HCA code doesn't separate unpack + transform (unpacks most data,
     * reads a subframe's spectra, transforms that subframe.
     *
     * Unpacking first takes a bit more memory (1 spectra per subframe) but test keys faster
     * (since unpack may fail with bad keys we can skip transform). For regular decoding, this
     * way somehow is slightly faster?  (~3-5%, extra compiler optimizations with reduced scope?) */

    res = clHCA_DecodeBlock_unpack(hca, data, size);
    if (res < 0)
        return res;
    clHCA_DecodeBlock_transform(hca);

    return res;
}

//--------------------------------------------------
// Decode 1st step
//--------------------------------------------------
/* curve/scale to quantized resolution */
static const unsigned char hcadecoder_invert_table[66] = {
    14,14,14,14,14,14,13,13, 13,13,13,13,12,12,12,12,
    12,12,11,11,11,11,11,11, 10,10,10,10,10,10,10, 9,
     9, 9, 9, 9, 9, 8, 8, 8,  8, 8, 8, 7, 6, 6, 5, 4,
     4, 4, 3, 3, 3, 2, 2, 2,  2, 1, 1, 1, 1, 1, 1, 1,
     1, 1,
    /* indexes after 56 are not defined in v2.0<= (manually clamped to 1) */
};

/* scalefactor-to-scaling table, generated from sqrt(128) * (2^(53/128))^(scale_factor - 63) */
static const unsigned int hcadequantizer_scaling_table_float_hex[64] = {
    0x342A8D26,0x34633F89,0x3497657D,0x34C9B9BE,0x35066491,0x353311C4,0x356E9910,0x359EF532,
    0x35D3CCF1,0x360D1ADF,0x363C034A,0x367A83B3,0x36A6E595,0x36DE60F5,0x371426FF,0x3745672A,
    0x37838359,0x37AF3B79,0x37E97C38,0x381B8D3A,0x384F4319,0x388A14D5,0x38B7FBF0,0x38F5257D,
    0x3923520F,0x39599D16,0x3990FA4D,0x39C12C4D,0x3A00B1ED,0x3A2B7A3A,0x3A647B6D,0x3A9837F0,
    0x3ACAD226,0x3B071F62,0x3B340AAF,0x3B6FE4BA,0x3B9FD228,0x3BD4F35B,0x3C0DDF04,0x3C3D08A4,
    0x3C7BDFED,0x3CA7CD94,0x3CDF9613,0x3D14F4F0,0x3D467991,0x3D843A29,0x3DB02F0E,0x3DEAC0C7,
    0x3E1C6573,0x3E506334,0x3E8AD4C6,0x3EB8FBAF,0x3EF67A41,0x3F243516,0x3F5ACB94,0x3F91C3D3,
    0x3FC238D2,0x400164D2,0x402C6897,0x4065B907,0x40990B88,0x40CBEC15,0x4107DB35,0x413504F3,
};
static const float* hcadequantizer_scaling_table_float = (const float*)hcadequantizer_scaling_table_float_hex;

/* in v2.0 lib index 0 is 0x00000000, but resolution 0 is only valid in v3.0 files */
static const unsigned int hcadequantizer_range_table_float_hex[16] = {
    0x3F800000,0x3F2AAAAB,0x3ECCCCCD,0x3E924925,0x3E638E39,0x3E3A2E8C,0x3E1D89D9,0x3E088889,
    0x3D842108,0x3D020821,0x3C810204,0x3C008081,0x3B804020,0x3B002008,0x3A801002,0x3A000801,
};
static const float* hcadequantizer_range_table_float = (const float*)hcadequantizer_range_table_float_hex;

/* get scale indexes to normalize dequantized coefficients */
static int unpack_scalefactors(stChannel* ch, clData* br, unsigned int hfr_group_count, unsigned int version) {
    int i;
    unsigned int cs_count = ch->coded_count;
    unsigned int extra_count;
    unsigned char delta_bits = bitreader_read(br, 3);

    /* added in v3.0 */
    if (ch->type == STEREO_SECONDARY || hfr_group_count <= 0 || version <= HCA_VERSION_V200) {
        extra_count = 0;
    }
    else {
        extra_count = hfr_group_count;
        cs_count = cs_count + extra_count;

        /* just in case */
        if (cs_count > HCA_SAMPLES_PER_SUBFRAME)
            return HCA_ERROR_UNPACK;
    }

    /* lib does check that cs_count is 2+ in fixed/delta case, but doesn't seem to affect anything */
    if (delta_bits >= 6) {
        /* fixed scalefactors */
        for (i = 0; i < cs_count; i++) {
            ch->scalefactors[i] = bitreader_read(br, 6);
        }
    }
    else if (delta_bits > 0) {
        /* delta scalefactors */
        const unsigned char expected_delta = (1 << delta_bits) - 1;
        unsigned char value = bitreader_read(br, 6);

        ch->scalefactors[0] = value;
        for (i = 1; i < cs_count; i++) {
            unsigned char delta = bitreader_read(br, delta_bits);

            if (delta == expected_delta) {
                value = bitreader_read(br, 6); /* encoded */
            }
            else {
                /* may happen with bad keycodes, scalefactors must be 6b indexes */
                int scalefactor_test = (int)value + ((int)delta - (int)(expected_delta >> 1));
                if (scalefactor_test < 0 || scalefactor_test >= 64) {
                    return HCA_ERROR_UNPACK;
                }

                value = value - (expected_delta >> 1) + delta; /* differential */
                value = value & 0x3F; /* v3.0 lib */

                //todo as negative better? (may roll otherwise?)
                //if (value >= 64)
                //    return HCA_ERROR_UNPACK;
            }
            ch->scalefactors[i] = value;
        }
    }
    else {
        /* no scalefactors */
        for (i = 0; i < HCA_SAMPLES_PER_SUBFRAME; i++) {
            ch->scalefactors[i] = 0;
        }
    }

    /* set derived HFR scales for v3.0 */
    for (i = 0; i < extra_count; i++) {
        ch->scalefactors[HCA_SAMPLES_PER_SUBFRAME - 1 - i] = ch->scalefactors[cs_count - i];
    }

    return HCA_RESULT_OK;
}

/* read intensity (for joint stereo R) or v2.0 high frequency scales (for regular channels) */
static int unpack_intensity(stChannel* ch, clData* br, unsigned int hfr_group_count, unsigned int version) {
    int i;

    if (ch->type == STEREO_SECONDARY) {
        /* read subframe intensity for channel pair (peek first for valid values, not sure why not consumed) */
        if (version <= HCA_VERSION_V200) {
            unsigned char value = bitreader_peek(br, 4);

            ch->intensity[0] = value;
            if (value < 15) {
                bitreader_skip(br, 4);
                for (i = 1; i < HCA_SUBFRAMES; i++) {
                    ch->intensity[i] = bitreader_read(br, 4);
                }
            }
            /* 15 may be an invalid value? index 15 is 0, but may imply "reuse last subframe's intensity".
             * no games seem to use 15 though */
            //else {
            //    return HCA_ERROR_UNPACK;
            //}
        }
        else {
            unsigned char value = bitreader_peek(br, 4);
            unsigned char delta_bits;

            if (value < 15) {
                bitreader_skip(br, 4);

                delta_bits = bitreader_read(br, 2); /* +1 */

                ch->intensity[0] = value;
                if (delta_bits == 3) { /* 3+1 = 4b */
                    /* fixed intensities */
                    for (i = 1; i < HCA_SUBFRAMES; i++) {
                        ch->intensity[i] = bitreader_read(br, 4);
                    }
                }
                else {
                    /* delta intensities */
                    unsigned char bmax = (2 << delta_bits) - 1;
                    unsigned char bits = delta_bits + 1;

                    for (i = 1; i < HCA_SUBFRAMES; i++) {
                        unsigned char delta = bitreader_read(br, bits);
                        if (delta == bmax) {
                            value = bitreader_read(br, 4); /* encoded */
                        }
                        else {
                            value = value - (bmax >> 1) + delta; /* differential */
                            if (value > 15) //todo check
                                return HCA_ERROR_UNPACK; /* not done in lib */
                        }

                        ch->intensity[i] = value;
                    }
                }
            }
            else {
                bitreader_skip(br, 4);
                for (i = 0; i < HCA_SUBFRAMES; i++) {
                    ch->intensity[i] = 7;
                }
            }
        }
    }
    else {
        /* read high frequency scalefactors (v3.0 uses derived values in unpack_scalefactors instead) */
        if (version <= HCA_VERSION_V200) {
            /* pointer in v2.0 lib for v2.0 files is base+stereo bands, while v3.0 lib for v2.0 files
             * is last HFR. No output difference but v3.0 files need that to handle HFR */
            //unsigned char* hfr_scales = &ch->scalefactors[base_band_count + stereo_band_count]; /* v2.0 lib */
            unsigned char* hfr_scales = &ch->scalefactors[128 - hfr_group_count]; /* v3.0 lib */

            for (i = 0; i < hfr_group_count; i++) {
                hfr_scales[i] = bitreader_read(br, 6);
            }
        }
    }

    return HCA_RESULT_OK;
}

/* get resolutions, that determines range of values per encoded spectrum coefficients */
static void calculate_resolution(stChannel* ch, unsigned int packed_noise_level, const unsigned char* ath_curve, unsigned int min_resolution, unsigned int max_resolution) {
    int i;
    unsigned int cr_count = ch->coded_count;
    unsigned int noise_count = 0;
    unsigned int valid_count = 0;

    for (i = 0; i < cr_count; i++) {
        unsigned char new_resolution = 0;
        unsigned char scalefactor = ch->scalefactors[i];

        if (scalefactor > 0) {
            /* curve values are 0 in v1.2>= so ath_curve is actually removed in CRI's code */
            int noise_level = ath_curve[i] + ((packed_noise_level + i) >> 8);
            int curve_position = noise_level + 1 - ((5 * scalefactor) >> 1);

            /* v2.0<= allows max 56 + sets rest to 1, while v3.0 table has 1 for 57..65 and
             * clamps to min_resolution below, so v2.0 files are still supported */
            if (curve_position < 0) {
                new_resolution = 15;
            }
            else if (curve_position <= 65) {
                new_resolution = hcadecoder_invert_table[curve_position];
            }
            else {
                new_resolution = 0;
            }

            /* added in v3.0 (before, min_resolution was always 1) */
            if (new_resolution > max_resolution)
                new_resolution = max_resolution;
            else if (new_resolution < min_resolution)
                new_resolution = min_resolution;

            /* save resolution 0 (not encoded) indexes (from 0..N), and regular indexes (from N..0) */
            if (new_resolution < 1) {
                ch->noises[noise_count] = i;
                noise_count++;
            }
            else {
                ch->noises[HCA_SAMPLES_PER_SUBFRAME - 1 - valid_count] = i;
                valid_count++;
            }
        }
        ch->resolution[i] = new_resolution;
    }

    ch->noise_count = noise_count;
    ch->valid_count = valid_count;

    memset(&ch->resolution[cr_count], 0, sizeof(ch->resolution[0]) * (HCA_SAMPLES_PER_SUBFRAME - cr_count));
}

/* get actual scales to dequantize based on saved scalefactors */
// HCADequantizer_CalculateGain
static void calculate_gain(stChannel* ch) {
    int i;
    unsigned int cg_count = ch->coded_count;

    for (i = 0; i < cg_count; i++) {
        float scalefactor_scale = hcadequantizer_scaling_table_float[ ch->scalefactors[i] ];
        float resolution_scale = hcadequantizer_range_table_float[ ch->resolution[i] ];
        ch->gain[i] = scalefactor_scale * resolution_scale;
    }
}

//--------------------------------------------------
// Decode 2nd step
//--------------------------------------------------
/* coded resolution to max bits */
static const unsigned char hcatbdecoder_max_bit_table[16] = {
    0,2,3,3,4,4,4,4, 5,6,7,8,9,10,11,12
};
/* bits used for quant codes */
static const unsigned char hcatbdecoder_read_bit_table[128] = {
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    1,1,2,2,0,0,0,0, 0,0,0,0,0,0,0,0,
    2,2,2,2,2,2,3,3, 0,0,0,0,0,0,0,0,
    2,2,3,3,3,3,3,3, 0,0,0,0,0,0,0,0,
    3,3,3,3,3,3,3,3, 3,3,3,3,3,3,4,4,
    3,3,3,3,3,3,3,3, 3,3,4,4,4,4,4,4,
    3,3,3,3,3,3,4,4, 4,4,4,4,4,4,4,4,
    3,3,4,4,4,4,4,4, 4,4,4,4,4,4,4,4,
};
/* code to quantized spectrum value */
static const float hcatbdecoder_read_val_table[128] = {
    +0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f, +0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,
    +0.0f,+0.0f,+1.0f,-1.0f,+0.0f,+0.0f,+0.0f,+0.0f, +0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,
    +0.0f,+0.0f,+1.0f,+1.0f,-1.0f,-1.0f,+2.0f,-2.0f, +0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,
    +0.0f,+0.0f,+1.0f,-1.0f,+2.0f,-2.0f,+3.0f,-3.0f, +0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,+0.0f,
    +0.0f,+0.0f,+1.0f,+1.0f,-1.0f,-1.0f,+2.0f,+2.0f, -2.0f,-2.0f,+3.0f,+3.0f,-3.0f,-3.0f,+4.0f,-4.0f,
    +0.0f,+0.0f,+1.0f,+1.0f,-1.0f,-1.0f,+2.0f,+2.0f, -2.0f,-2.0f,+3.0f,-3.0f,+4.0f,-4.0f,+5.0f,-5.0f,
    +0.0f,+0.0f,+1.0f,+1.0f,-1.0f,-1.0f,+2.0f,-2.0f, +3.0f,-3.0f,+4.0f,-4.0f,+5.0f,-5.0f,+6.0f,-6.0f,
    +0.0f,+0.0f,+1.0f,-1.0f,+2.0f,-2.0f,+3.0f,-3.0f, +4.0f,-4.0f,+5.0f,-5.0f,+6.0f,-6.0f,+7.0f,-7.0f,
};

/* read spectral coefficients in the bitstream */
static void dequantize_coefficients(stChannel* ch, clData* br, int subframe) {
    int i;
    unsigned int cc_count = ch->coded_count;

    for (i = 0; i < cc_count; i++) {
        float qc;
        unsigned char resolution = ch->resolution[i];
        unsigned char bits = hcatbdecoder_max_bit_table[resolution];
        unsigned int code = bitreader_read(br, bits);

        if (resolution > 7) {
            /* parse values in sign-magnitude form (lowest bit = sign) */
            int signed_code = (1 - ((code & 1) << 1)) * (code >> 1); /* move sign from low to up */
            if (signed_code == 0)
                bitreader_skip(br, -1); /* zero uses one less bit since it has no sign */
            qc = (float)signed_code;
        }
        else {
            /* use prefix codebooks for lower resolutions */
            int index = (resolution << 4) + code;
            int skip = hcatbdecoder_read_bit_table[index] - bits;
            bitreader_skip(br, skip);
            qc = hcatbdecoder_read_val_table[index];
        }

        /* dequantize coef with gain */
        ch->spectra[subframe][i] = ch->gain[i] * qc;
    }

    /* clean rest of spectra */
    memset(&ch->spectra[subframe][cc_count], 0, sizeof(ch->spectra[subframe][0]) * (HCA_SAMPLES_PER_SUBFRAME - cc_count));
}


//--------------------------------------------------
// Decode 3rd step
//--------------------------------------------------
/* in lib this table does start with a single 0.0 and adds + 63 below
 * (other decoders start with two 0.0 and add + 64 below, that should be equivalent) */
static const unsigned int hcadecoder_scale_conversion_table_hex[128] = {
    0x00000000,0x32A0B051,0x32D61B5E,0x330EA43A,0x333E0F68,0x337D3E0C,0x33A8B6D5,0x33E0CCDF,
    0x3415C3FF,0x34478D75,0x3484F1F6,0x34B123F6,0x34EC0719,0x351D3EDA,0x355184DF,0x358B95C2,
    0x35B9FCD2,0x35F7D0DF,0x36251958,0x365BFBB8,0x36928E72,0x36C346CD,0x370218AF,0x372D583F,
    0x3766F85B,0x3799E046,0x37CD078C,0x3808980F,0x38360094,0x38728177,0x38A18FAF,0x38D744FD,
    0x390F6A81,0x393F179A,0x397E9E11,0x39A9A15B,0x39E2055B,0x3A16942D,0x3A48A2D8,0x3A85AAC3,
    0x3AB21A32,0x3AED4F30,0x3B1E196E,0x3B52A81E,0x3B8C57CA,0x3BBAFF5B,0x3BF9295A,0x3C25FED7,
    0x3C5D2D82,0x3C935A2B,0x3CC4563F,0x3D02CD87,0x3D2E4934,0x3D68396A,0x3D9AB62B,0x3DCE248C,
    0x3E0955EE,0x3E36FD92,0x3E73D290,0x3EA27043,0x3ED87039,0x3F1031DC,0x3F40213B,0x3F800000,

    0x3FAA8D26,0x3FE33F89,0x4017657D,0x4049B9BE,0x40866491,0x40B311C4,0x40EE9910,0x411EF532,
    0x4153CCF1,0x418D1ADF,0x41BC034A,0x41FA83B3,0x4226E595,0x425E60F5,0x429426FF,0x42C5672A,
    0x43038359,0x432F3B79,0x43697C38,0x439B8D3A,0x43CF4319,0x440A14D5,0x4437FBF0,0x4475257D,
    0x44A3520F,0x44D99D16,0x4510FA4D,0x45412C4D,0x4580B1ED,0x45AB7A3A,0x45E47B6D,0x461837F0,
    0x464AD226,0x46871F62,0x46B40AAF,0x46EFE4BA,0x471FD228,0x4754F35B,0x478DDF04,0x47BD08A4,
    0x47FBDFED,0x4827CD94,0x485F9613,0x4894F4F0,0x48C67991,0x49043A29,0x49302F0E,0x496AC0C7,
    0x499C6573,0x49D06334,0x4A0AD4C6,0x4A38FBAF,0x4A767A41,0x4AA43516,0x4ADACB94,0x4B11C3D3,
    0x4B4238D2,0x4B8164D2,0x4BAC6897,0x4BE5B907,0x4C190B88,0x4C4BEC15,0x00000000,0x00000000,
};
static const float* hcadecoder_scale_conversion_table = (const float*)hcadecoder_scale_conversion_table_hex;

/* recreate resolution 0 coefs (not encoded) with pseudo-random noise based on
 * other coefs/scales (probably similar to AAC's perceptual noise substitution) */
static void reconstruct_noise(stChannel* ch, unsigned int min_resolution, unsigned int ms_stereo, unsigned int* random_p, int subframe) {
    if (min_resolution > 0) /* added in v3.0 */
        return;
    if (ch->valid_count <= 0 || ch->noise_count <= 0)
        return;
    if (!(!ms_stereo || ch->type == STEREO_PRIMARY))
        return;

    {
        int i;
        int random_index, noise_index, valid_index, sf_noise, sf_valid, sc_index;
        unsigned int random = *random_p;

        for (i = 0; i < ch->noise_count; i++) {
            random = 0x343FD * random + 0x269EC3; /* typical rand() */

            random_index = HCA_SAMPLES_PER_SUBFRAME - ch->valid_count + (((random & 0x7FFF) * ch->valid_count) >> 15); /* can't go over 128 */

            /* points to next resolution 0 index and random non-resolution 0 index */
            noise_index = ch->noises[i];
            valid_index = ch->noises[random_index];

            /* get final scale index */
            sf_noise = ch->scalefactors[noise_index];
            sf_valid = ch->scalefactors[valid_index];
            sc_index = (sf_noise - sf_valid + 62) & ~((sf_noise - sf_valid + 62) >> 31);

            ch->spectra[subframe][noise_index] = 
                hcadecoder_scale_conversion_table[sc_index] * ch->spectra[subframe][valid_index];
        }

        *random_p = random; /* lib saves this in the bitreader, maybe for simplified passing around */
    }
}

/* recreate missing coefs in high bands based on lower bands (probably similar to AAC's spectral band replication) */
static void reconstruct_high_frequency(stChannel* ch, unsigned int hfr_group_count, unsigned int bands_per_hfr_group,
        unsigned int stereo_band_count, unsigned int base_band_count, unsigned int total_band_count, unsigned int version, int subframe) {
    if (bands_per_hfr_group == 0) /* added in v2.0, skipped in v2.0 files with 0 bands too */
        return;
    if (ch->type == STEREO_SECONDARY)
        return;

    {
        int i;
        int group, group_limit;
        int start_band = stereo_band_count + base_band_count;
        int highband = start_band;
        int lowband = start_band - 1;
        int sc_index;
        //unsigned char* hfr_scales = &ch->scalefactors[base_band_count + stereo_band_count]; /* v2.0 lib */
        unsigned char* hfr_scales = &ch->scalefactors[128 - hfr_group_count]; /* v3.0 lib */

        if (version <= HCA_VERSION_V200) {
            group_limit = hfr_group_count;
        }
        else {
            group_limit = (hfr_group_count >= 0) ? hfr_group_count : hfr_group_count + 1; /* ??? */
            group_limit = group_limit >> 1;
        }

        for (group = 0; group < hfr_group_count; group++) {
            int lowband_sub = (group < group_limit) ? 1 : 0; /* move lowband towards 0 until group reachs limit */

            for (i = 0; i < bands_per_hfr_group; i++) {
                if (highband >= total_band_count || lowband < 0)
                    break;

                sc_index = hfr_scales[group] - ch->scalefactors[lowband] + 63;
                sc_index = sc_index & ~(sc_index >> 31); /* clamped in v3.0 lib (in theory 6b sf are 0..128) */

                ch->spectra[subframe][highband] = hcadecoder_scale_conversion_table[sc_index] * ch->spectra[subframe][lowband];

                highband += 1;
                lowband -= lowband_sub;
            }
        }

        /* last spectrum coefficient is 0 (normally highband = 128, but perhaps could 'break' before) */
        ch->spectra[subframe][highband - 1] = 0.0f;
    }
}

//--------------------------------------------------
// Decode 4th step
//--------------------------------------------------
/* index to scale */
static const unsigned int hcadecoder_intensity_ratio_table_hex[16] = { /* max 4b */
    0x40000000,0x3FEDB6DB,0x3FDB6DB7,0x3FC92492,0x3FB6DB6E,0x3FA49249,0x3F924925,0x3F800000,
    0x3F5B6DB7,0x3F36DB6E,0x3F124925,0x3EDB6DB7,0x3E924925,0x3E124925,0x00000000,0x00000000,
};
static const float* hcadecoder_intensity_ratio_table = (const float*)hcadecoder_intensity_ratio_table_hex;

/* restore L/R bands based on channel coef + panning */
static void apply_intensity_stereo(stChannel* ch_pair, int subframe, unsigned int base_band_count, unsigned int total_band_count) {
    if (ch_pair[0].type != STEREO_PRIMARY)
        return;

    {
        int band;
        float ratio_l = hcadecoder_intensity_ratio_table[ ch_pair[1].intensity[subframe] ];
        float ratio_r = 2.0f - ratio_l; /* correct, though other decoders substract 2.0 (it does use 'fsubr 2.0' and such) */
        float* sp_l = &ch_pair[0].spectra[subframe][0];
        float* sp_r = &ch_pair[1].spectra[subframe][0];

        for (band = base_band_count; band < total_band_count; band++) {
            float coef_l = sp_l[band] * ratio_l;
            float coef_r = sp_l[band] * ratio_r;
            sp_l[band] = coef_l;
            sp_r[band] = coef_r;
        }
    }
}

/* restore L/R bands based on mid channel + side differences */
static void apply_ms_stereo(stChannel* ch_pair, unsigned int ms_stereo, unsigned int base_band_count, unsigned int total_band_count, int subframe) {
    if (!ms_stereo) /* added in v3.0 */
        return;
    if (ch_pair[0].type != STEREO_PRIMARY)
        return;

    {
        int band;
        const float ratio = 0.70710676908493; /* 0x3F3504F3 */
        float* sp_l = &ch_pair[0].spectra[subframe][0];
        float* sp_r = &ch_pair[1].spectra[subframe][0];

        for (band = base_band_count; band < total_band_count; band++) {
            float coef_l = (sp_l[band] + sp_r[band]) * ratio;
            float coef_r = (sp_l[band] - sp_r[band]) * ratio;
            sp_l[band] = coef_l;
            sp_r[band] = coef_r;
        }
    }
}

//--------------------------------------------------
// Decode 5th step
//--------------------------------------------------
static const unsigned int sin_tables_hex[7][64] = {
    {
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
        0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,0x3DA73D75,
    },{
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
        0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,0x3F7B14BE,0x3F54DB31,
    },{
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
        0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,0x3F7EC46D,0x3F74FA0B,0x3F61C598,0x3F45E403,
    },{
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
        0x3F7FB10F,0x3F7D3AAC,0x3F7853F8,0x3F710908,0x3F676BD8,0x3F5B941A,0x3F4D9F02,0x3F3DAEF9,
    },{
        0x3F7FEC43,0x3F7F4E6D,0x3F7E1324,0x3F7C3B28,0x3F79C79D,0x3F76BA07,0x3F731447,0x3F6ED89E,
        0x3F6A09A7,0x3F64AA59,0x3F5EBE05,0x3F584853,0x3F514D3D,0x3F49D112,0x3F41D870,0x3F396842,
        0x3F7FEC43,0x3F7F4E6D,0x3F7E1324,0x3F7C3B28,0x3F79C79D,0x3F76BA07,0x3F731447,0x3F6ED89E,
        0x3F6A09A7,0x3F64AA59,0x3F5EBE05,0x3F584853,0x3F514D3D,0x3F49D112,0x3F41D870,0x3F396842,
        0x3F7FEC43,0x3F7F4E6D,0x3F7E1324,0x3F7C3B28,0x3F79C79D,0x3F76BA07,0x3F731447,0x3F6ED89E,
        0x3F6A09A7,0x3F64AA59,0x3F5EBE05,0x3F584853,0x3F514D3D,0x3F49D112,0x3F41D870,0x3F396842,
        0x3F7FEC43,0x3F7F4E6D,0x3F7E1324,0x3F7C3B28,0x3F79C79D,0x3F76BA07,0x3F731447,0x3F6ED89E,
        0x3F6A09A7,0x3F64AA59,0x3F5EBE05,0x3F584853,0x3F514D3D,0x3F49D112,0x3F41D870,0x3F396842,
    },{
        0x3F7FFB11,0x3F7FD397,0x3F7F84AB,0x3F7F0E58,0x3F7E70B0,0x3F7DABCC,0x3F7CBFC9,0x3F7BACCD,
        0x3F7A7302,0x3F791298,0x3F778BC5,0x3F75DEC6,0x3F740BDD,0x3F721352,0x3F6FF573,0x3F6DB293,
        0x3F6B4B0C,0x3F68BF3C,0x3F660F88,0x3F633C5A,0x3F604621,0x3F5D2D53,0x3F59F26A,0x3F5695E5,
        0x3F531849,0x3F4F7A1F,0x3F4BBBF8,0x3F47DE65,0x3F43E200,0x3F3FC767,0x3F3B8F3B,0x3F373A23,
        0x3F7FFB11,0x3F7FD397,0x3F7F84AB,0x3F7F0E58,0x3F7E70B0,0x3F7DABCC,0x3F7CBFC9,0x3F7BACCD,
        0x3F7A7302,0x3F791298,0x3F778BC5,0x3F75DEC6,0x3F740BDD,0x3F721352,0x3F6FF573,0x3F6DB293,
        0x3F6B4B0C,0x3F68BF3C,0x3F660F88,0x3F633C5A,0x3F604621,0x3F5D2D53,0x3F59F26A,0x3F5695E5,
        0x3F531849,0x3F4F7A1F,0x3F4BBBF8,0x3F47DE65,0x3F43E200,0x3F3FC767,0x3F3B8F3B,0x3F373A23,
    },{
        0x3F7FFEC4,0x3F7FF4E6,0x3F7FE129,0x3F7FC38F,0x3F7F9C18,0x3F7F6AC7,0x3F7F2F9D,0x3F7EEA9D,
        0x3F7E9BC9,0x3F7E4323,0x3F7DE0B1,0x3F7D7474,0x3F7CFE73,0x3F7C7EB0,0x3F7BF531,0x3F7B61FC,
        0x3F7AC516,0x3F7A1E84,0x3F796E4E,0x3F78B47B,0x3F77F110,0x3F772417,0x3F764D97,0x3F756D97,
        0x3F748422,0x3F73913F,0x3F7294F8,0x3F718F57,0x3F708066,0x3F6F6830,0x3F6E46BE,0x3F6D1C1D,
        0x3F6BE858,0x3F6AAB7B,0x3F696591,0x3F6816A8,0x3F66BECC,0x3F655E0B,0x3F63F473,0x3F628210,
        0x3F6106F2,0x3F5F8327,0x3F5DF6BE,0x3F5C61C7,0x3F5AC450,0x3F591E6A,0x3F577026,0x3F55B993,
        0x3F53FAC3,0x3F5233C6,0x3F5064AF,0x3F4E8D90,0x3F4CAE79,0x3F4AC77F,0x3F48D8B3,0x3F46E22A,
        0x3F44E3F5,0x3F42DE29,0x3F40D0DA,0x3F3EBC1B,0x3F3CA003,0x3F3A7CA4,0x3F385216,0x3F36206C,
    }
};
static const unsigned int cos_tables_hex[7][64]={
    {
        0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,
        0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,
        0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,
        0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,
        0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,
        0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,
        0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,
        0x3D0A8BD4,0xBD0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0xBD0A8BD4,0x3D0A8BD4,0x3D0A8BD4,0xBD0A8BD4,
    },{
        0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,
        0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,
        0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,
        0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,
        0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,
        0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,
        0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,
        0x3E47C5C2,0x3F0E39DA,0xBE47C5C2,0xBF0E39DA,0xBE47C5C2,0xBF0E39DA,0x3E47C5C2,0x3F0E39DA,
    },{
        0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,
        0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,
        0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,
        0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,
        0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,
        0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,
        0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,
        0x3DC8BD36,0x3E94A031,0x3EF15AEA,0x3F226799,0xBDC8BD36,0xBE94A031,0xBEF15AEA,0xBF226799,
    },{
        0xBD48FB30,0xBE164083,0xBE78CFCC,0xBEAC7CD4,0xBEDAE880,0xBF039C3D,0xBF187FC0,0xBF2BEB4A,
        0x3D48FB30,0x3E164083,0x3E78CFCC,0x3EAC7CD4,0x3EDAE880,0x3F039C3D,0x3F187FC0,0x3F2BEB4A,
        0x3D48FB30,0x3E164083,0x3E78CFCC,0x3EAC7CD4,0x3EDAE880,0x3F039C3D,0x3F187FC0,0x3F2BEB4A,
        0xBD48FB30,0xBE164083,0xBE78CFCC,0xBEAC7CD4,0xBEDAE880,0xBF039C3D,0xBF187FC0,0xBF2BEB4A,
        0x3D48FB30,0x3E164083,0x3E78CFCC,0x3EAC7CD4,0x3EDAE880,0x3F039C3D,0x3F187FC0,0x3F2BEB4A,
        0xBD48FB30,0xBE164083,0xBE78CFCC,0xBEAC7CD4,0xBEDAE880,0xBF039C3D,0xBF187FC0,0xBF2BEB4A,
        0xBD48FB30,0xBE164083,0xBE78CFCC,0xBEAC7CD4,0xBEDAE880,0xBF039C3D,0xBF187FC0,0xBF2BEB4A,
        0x3D48FB30,0x3E164083,0x3E78CFCC,0x3EAC7CD4,0x3EDAE880,0x3F039C3D,0x3F187FC0,0x3F2BEB4A,
    },{
        0xBCC90AB0,0xBD96A905,0xBDFAB273,0xBE2F10A2,0xBE605C13,0xBE888E93,0xBEA09AE5,0xBEB8442A,
        0xBECF7BCA,0xBEE63375,0xBEFC5D27,0xBF08F59B,0xBF13682A,0xBF1D7FD1,0xBF273656,0xBF3085BB,
        0x3CC90AB0,0x3D96A905,0x3DFAB273,0x3E2F10A2,0x3E605C13,0x3E888E93,0x3EA09AE5,0x3EB8442A,
        0x3ECF7BCA,0x3EE63375,0x3EFC5D27,0x3F08F59B,0x3F13682A,0x3F1D7FD1,0x3F273656,0x3F3085BB,
        0x3CC90AB0,0x3D96A905,0x3DFAB273,0x3E2F10A2,0x3E605C13,0x3E888E93,0x3EA09AE5,0x3EB8442A,
        0x3ECF7BCA,0x3EE63375,0x3EFC5D27,0x3F08F59B,0x3F13682A,0x3F1D7FD1,0x3F273656,0x3F3085BB,
        0xBCC90AB0,0xBD96A905,0xBDFAB273,0xBE2F10A2,0xBE605C13,0xBE888E93,0xBEA09AE5,0xBEB8442A,
        0xBECF7BCA,0xBEE63375,0xBEFC5D27,0xBF08F59B,0xBF13682A,0xBF1D7FD1,0xBF273656,0xBF3085BB,
    },{
        0xBC490E90,0xBD16C32C,0xBD7B2B74,0xBDAFB680,0xBDE1BC2E,0xBE09CF86,0xBE22ABB6,0xBE3B6ECF,
        0xBE541501,0xBE6C9A7F,0xBE827DC0,0xBE8E9A22,0xBE9AA086,0xBEA68F12,0xBEB263EF,0xBEBE1D4A,
        0xBEC9B953,0xBED53641,0xBEE0924F,0xBEEBCBBB,0xBEF6E0CB,0xBF00E7E4,0xBF064B82,0xBF0B9A6B,
        0xBF10D3CD,0xBF15F6D9,0xBF1B02C6,0xBF1FF6CB,0xBF24D225,0xBF299415,0xBF2E3BDE,0xBF32C8C9,
        0x3C490E90,0x3D16C32C,0x3D7B2B74,0x3DAFB680,0x3DE1BC2E,0x3E09CF86,0x3E22ABB6,0x3E3B6ECF,
        0x3E541501,0x3E6C9A7F,0x3E827DC0,0x3E8E9A22,0x3E9AA086,0x3EA68F12,0x3EB263EF,0x3EBE1D4A,
        0x3EC9B953,0x3ED53641,0x3EE0924F,0x3EEBCBBB,0x3EF6E0CB,0x3F00E7E4,0x3F064B82,0x3F0B9A6B,
        0x3F10D3CD,0x3F15F6D9,0x3F1B02C6,0x3F1FF6CB,0x3F24D225,0x3F299415,0x3F2E3BDE,0x3F32C8C9,
    },{
        0xBBC90F88,0xBC96C9B6,0xBCFB49BA,0xBD2FE007,0xBD621469,0xBD8A200A,0xBDA3308C,0xBDBC3AC3,
        0xBDD53DB9,0xBDEE3876,0xBE039502,0xBE1008B7,0xBE1C76DE,0xBE28DEFC,0xBE354098,0xBE419B37,
        0xBE4DEE60,0xBE5A3997,0xBE667C66,0xBE72B651,0xBE7EE6E1,0xBE8586CE,0xBE8B9507,0xBE919DDD,
        0xBE97A117,0xBE9D9E78,0xBEA395C5,0xBEA986C4,0xBEAF713A,0xBEB554EC,0xBEBB31A0,0xBEC1071E,
        0xBEC6D529,0xBECC9B8B,0xBED25A09,0xBED8106B,0xBEDDBE79,0xBEE363FA,0xBEE900B7,0xBEEE9479,
        0xBEF41F07,0xBEF9A02D,0xBEFF17B2,0xBF0242B1,0xBF04F484,0xBF07A136,0xBF0A48AD,0xBF0CEAD0,
        0xBF0F8784,0xBF121EB0,0xBF14B039,0xBF173C07,0xBF19C200,0xBF1C420C,0xBF1EBC12,0xBF212FF9,
        0xBF239DA9,0xBF26050A,0xBF286605,0xBF2AC082,0xBF2D1469,0xBF2F61A5,0xBF31A81D,0xBF33E7BC,
    }
};

/* HCA window function, close to a KBD window with an alpha of around 3.82 (similar to AAC/Vorbis) */
static const unsigned int hcaimdct_window_float_hex[128] = {
    0x3A3504F0,0x3B0183B8,0x3B70C538,0x3BBB9268,0x3C04A809,0x3C308200,0x3C61284C,0x3C8B3F17,
    0x3CA83992,0x3CC77FBD,0x3CE91110,0x3D0677CD,0x3D198FC4,0x3D2DD35C,0x3D434643,0x3D59ECC1,
    0x3D71CBA8,0x3D85741E,0x3D92A413,0x3DA078B4,0x3DAEF522,0x3DBE1C9E,0x3DCDF27B,0x3DDE7A1D,
    0x3DEFB6ED,0x3E00D62B,0x3E0A2EDA,0x3E13E72A,0x3E1E00B1,0x3E287CF2,0x3E335D55,0x3E3EA321,
    0x3E4A4F75,0x3E56633F,0x3E62DF37,0x3E6FC3D1,0x3E7D1138,0x3E8563A2,0x3E8C72B7,0x3E93B561,
    0x3E9B2AEF,0x3EA2D26F,0x3EAAAAAB,0x3EB2B222,0x3EBAE706,0x3EC34737,0x3ECBD03D,0x3ED47F46,
    0x3EDD5128,0x3EE6425C,0x3EEF4EFF,0x3EF872D7,0x3F00D4A9,0x3F0576CA,0x3F0A1D3B,0x3F0EC548,
    0x3F136C25,0x3F180EF2,0x3F1CAAC2,0x3F213CA2,0x3F25C1A5,0x3F2A36E7,0x3F2E9998,0x3F32E705,

    0xBF371C9E,0xBF3B37FE,0xBF3F36F2,0xBF431780,0xBF46D7E6,0xBF4A76A4,0xBF4DF27C,0xBF514A6F,
    0xBF547DC5,0xBF578C03,0xBF5A74EE,0xBF5D3887,0xBF5FD707,0xBF6250DA,0xBF64A699,0xBF66D908,
    0xBF68E90E,0xBF6AD7B1,0xBF6CA611,0xBF6E5562,0xBF6FE6E7,0xBF715BEF,0xBF72B5D1,0xBF73F5E6,
    0xBF751D89,0xBF762E13,0xBF7728D7,0xBF780F20,0xBF78E234,0xBF79A34C,0xBF7A5397,0xBF7AF439,
    0xBF7B8648,0xBF7C0ACE,0xBF7C82C8,0xBF7CEF26,0xBF7D50CB,0xBF7DA88E,0xBF7DF737,0xBF7E3D86,
    0xBF7E7C2A,0xBF7EB3CC,0xBF7EE507,0xBF7F106C,0xBF7F3683,0xBF7F57CA,0xBF7F74B6,0xBF7F8DB6,
    0xBF7FA32E,0xBF7FB57B,0xBF7FC4F6,0xBF7FD1ED,0xBF7FDCAD,0xBF7FE579,0xBF7FEC90,0xBF7FF22E,
    0xBF7FF688,0xBF7FF9D0,0xBF7FFC32,0xBF7FFDDA,0xBF7FFEED,0xBF7FFF8F,0xBF7FFFDF,0xBF7FFFFC,
};
static const float* hcaimdct_window_float = (const float*)hcaimdct_window_float_hex;

/* apply DCT-IV to dequantized spectra to get final samples */
//HCAIMDCT_Transform
static void imdct_transform(stChannel* ch, int subframe) {
    static const unsigned int size = HCA_SAMPLES_PER_SUBFRAME;
    static const unsigned int half = HCA_SAMPLES_PER_SUBFRAME / 2;
    static const unsigned int mdct_bits = HCA_MDCT_BITS;
    unsigned int i, j, k;

    /* This IMDCT (supposedly standard) is all too crafty for me to simplify, see VGAudio (Mdct.Dct4). */

    /* pre-pre-rotation(?) */
    {
        unsigned int count1 = 1;
        unsigned int count2 = half;
        float* temp1 = &ch->spectra[subframe][0];
        float* temp2 = &ch->temp[0];

        for (i = 0; i < mdct_bits; i++) {
            float* swap;
            float* d1 = &temp2[0];
            float* d2 = &temp2[count2];

            for (j = 0; j < count1; j++) {
                for (k = 0; k < count2; k++) {
                    float a = *(temp1++);
                    float b = *(temp1++);
                    *(d1++) = a + b;
                    *(d2++) = a - b;
                }
                d1 += count2;
                d2 += count2;
            }
            swap = temp1 - HCA_SAMPLES_PER_SUBFRAME; /* move spectra or temp to beginning */
            temp1 = temp2;
            temp2 = swap;

            count1 = count1 << 1;
            count2 = count2 >> 1;
        }
    }

    {
        unsigned int count1 = half;
        unsigned int count2 = 1;
        float* temp1 = &ch->temp[0];
        float* temp2 = &ch->spectra[subframe][0];

        for (i = 0; i < mdct_bits; i++) {
            const float* sin_table = (const float*) sin_tables_hex[i];//todo cleanup
            const float* cos_table = (const float*) cos_tables_hex[i];
            float* swap;
            float* d1 = &temp2[0];
            float* d2 = &temp2[count2 * 2 - 1];
            const float* s1 = &temp1[0];
            const float* s2 = &temp1[count2];

            for (j = 0; j < count1; j++) {
                for (k = 0; k < count2; k++) {
                    float a = *(s1++);
                    float b = *(s2++);
                    float sin = *(sin_table++);
                    float cos = *(cos_table++);
                    *(d1++) = a * sin - b * cos;
                    *(d2--) = a * cos + b * sin;
                }
                s1 += count2;
                s2 += count2;
                d1 += count2;
                d2 += count2 * 3;
            }
            swap = temp1;
            temp1 = temp2;
            temp2 = swap;

            count1 = count1 >> 1;
            count2 = count2 << 1;
        }
#if 0
        /* copy dct */
        /* (with the above optimization spectra is already modified, so this is redundant) */
        for (i = 0; i < size; i++) {
            ch->dct[i] = ch->spectra[subframe][i];
        }
#endif
    }

    /* update output/imdct with overlapped window (lib fuses this with the above) */
    {
        const float* dct = &ch->spectra[subframe][0]; //ch->dct;
        const float* prev = &ch->imdct_previous[0];

        for (i = 0; i < half; i++) {
            ch->wave[subframe][i] = hcaimdct_window_float[i] * dct[i + half] + prev[i];
            ch->wave[subframe][i + half] = hcaimdct_window_float[i + half] * dct[size - 1 - i] - prev[i + half];
            ch->imdct_previous[i] = hcaimdct_window_float[size - 1 - i] * dct[half - i - 1];
            ch->imdct_previous[i + half] = hcaimdct_window_float[half - i - 1] * dct[i];
        }
#if 0
        /* over-optimized IMDCT window (for reference), barely noticeable even when decoding hundred of files */
        const float* imdct_window = hcaimdct_window_float;
        const float* dct;
        float* imdct_previous;
        float* wave = ch->wave[subframe];

        dct = &ch->dct[half];
        imdct_previous = ch->imdct_previous;
        for (i = 0; i < half; i++) {
            *(wave++) = *(dct++) * *(imdct_window++) + *(imdct_previous++);
        }
        for (i = 0; i < half; i++) {
            *(wave++) = *(imdct_window++) * *(--dct) - *(imdct_previous++);
        }
        /* implicit: imdct_window pointer is now at end */
        dct = &ch->dct[half - 1];
        imdct_previous = ch->imdct_previous;
        for (i = 0; i < half; i++) {
            *(imdct_previous++) = *(--imdct_window) * *(dct--);
        }
        for (i = 0; i < half; i++) {
            *(imdct_previous++) = *(--imdct_window) * *(++dct) ;
        }
#endif
    }
}

// Adapated from K0lb3 FPNG python wrapper code.
PyObject *py_decode_err(int code)
{
    switch (code)
    {
    case -1:
        PyErr_SetString(PyExc_ValueError, "Header decoding error, the header is not a valic HCA header.");
        break;
    case -2:
        PyErr_SetString(PyExc_ValueError, "Decoding error, either an incorrect key or an unknown exception.");
        break;
    case -3:
        PyErr_SetString(PyExc_ValueError, "Encoding error.");
        break;
    case -4:
        PyErr_SetString(PyExc_ValueError, "Encoding error, band level is in the negatives, unsupported.");
        break;
    case -5:
        PyErr_SetString(PyExc_ValueError, "No PCM data is available in the WAV file.");
        break;
    }
    return NULL;
}

// Taken from Nyagamon's clHCA code.
struct stWAVEHeader {
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
struct stWAVEsmpl {
    char smpl[4];
    unsigned int smplSize;
    unsigned int manufacturer;
    unsigned int product;
    unsigned int samplePeriod;
    unsigned int MIDIUnityNote;
    unsigned int MIDIPitchFraction;
    unsigned int SMPTEFormat;
    unsigned int SMPTEOffset;
    unsigned int sampleLoops;
    unsigned int samplerData;
    unsigned int loop_Identifier;
    unsigned int loop_Type;
    unsigned int loop_Start;
    unsigned int loop_End;
    unsigned int loop_Fraction;
    unsigned int loop_PlayCount;
};
struct stWAVEnote {
    char note[4];
    unsigned int noteSize;
    unsigned int dwName;
};
struct stWAVEdata {
    char data[4];
    unsigned int dataSize;
};

/**
 * @brief Encoding related functions are here.
 * A partial port of VGAudio HCA encoder.
 * 
 */

int GetNextMultiple(int value, int multiple){
    if (multiple <= 0)
        return value;

    if (value % multiple == 0)
        return value;

    return value + multiple - value % multiple;
}

int DivideByRoundUp(int value, int divisor){
    return (int)std::ceil((float)value / divisor);
}

static const int ShuffleTables[] = {
    0, 64, 96, 32, 48, 112, 80, 16, 24, 88, 120, 56, 40, 104, 72, 8, 12, 76, 108,
    44, 60, 124, 92, 28, 20, 84, 116, 52, 36, 100, 68, 4, 6, 70, 102, 38, 54, 118,
    86, 22, 30, 94, 126, 62, 46, 110, 78, 14, 10, 74, 106, 42, 58, 122, 90, 26, 18,
    82, 114, 50, 34, 98, 66, 2, 3, 67, 99, 35, 51, 115, 83, 19, 27, 91, 123, 59,
    43, 107, 75, 11, 15, 79, 111, 47, 63, 127, 95, 31, 23, 87, 119, 55, 39, 103,
    71, 7, 5, 69, 101, 37, 53, 117, 85, 21, 29, 93, 125, 61, 45, 109, 77, 13, 9,
    73, 105, 41, 57, 121, 89, 25, 17, 81, 113, 49, 33, 97, 65, 1
};

static const float hcamdct_window_float[128] = {
    6.90533780e-4f, 1.97623484e-3f, 3.67386453e-3f, 5.72424009e-3f, 8.09670333e-3f, 1.07731819e-2f, 1.37425177e-2f, 1.69978570e-2f,
    2.05352642e-2f, 2.43529025e-2f, 2.84505188e-2f, 3.28290947e-2f, 3.74906212e-2f, 4.24378961e-2f, 4.76744287e-2f, 5.32043017e-2f,
    5.90321124e-2f, 6.51628822e-2f, 7.16020092e-2f, 7.83552229e-2f, 8.54284912e-2f, 9.28280205e-2f, 1.00560151e-1f, 1.08631350e-1f,
    1.17048122e-1f, 1.25816986e-1f, 1.34944350e-1f, 1.44436508e-1f, 1.54299513e-1f, 1.64539129e-1f, 1.75160721e-1f, 1.86169162e-1f,
    1.97568730e-1f, 2.09362969e-1f, 2.21554622e-1f, 2.34145418e-1f, 2.47135997e-1f, 2.60525763e-1f, 2.74312705e-1f, 2.88493186e-1f,
    3.03061932e-1f, 3.18011731e-1f, 3.33333343e-1f, 3.49015296e-1f, 3.65043819e-1f, 3.81402701e-1f, 3.98073107e-1f, 4.15033519e-1f,
    4.32259798e-1f, 4.49725032e-1f, 4.67399567e-1f, 4.85251158e-1f, 5.03244936e-1f, 5.21343827e-1f, 5.39508522e-1f, 5.57697773e-1f,
    5.75868905e-1f, 5.93978047e-1f, 6.11980557e-1f, 6.29831433e-1f, 6.47486031e-1f, 6.64900243e-1f, 6.82031155e-1f, 6.98837578e-1f,
    7.15280414e-1f, 7.31323123e-1f, 7.46932149e-1f, 7.62077332e-1f, 7.76731849e-1f, 7.90872812e-1f, 8.04481268e-1f, 8.17542017e-1f,
    8.30044091e-1f, 8.41980159e-1f, 8.53346705e-1f, 8.64143789e-1f, 8.74374807e-1f, 8.84046197e-1f, 8.93167078e-1f, 9.01749134e-1f,
    9.09806132e-1f, 9.17353690e-1f, 9.24408972e-1f, 9.30990338e-1f, 9.37117040e-1f, 9.42809045e-1f, 9.48086798e-1f, 9.52970862e-1f,
    9.57481921e-1f, 9.61640537e-1f, 9.65466917e-1f, 9.68980789e-1f, 9.72201586e-1f, 9.75147963e-1f, 9.77837980e-1f, 9.80289042e-1f,
    9.82517719e-1f, 9.84539866e-1f, 9.86370564e-1f, 9.88024116e-1f, 9.89514053e-1f, 9.90853190e-1f, 9.92053449e-1f, 9.93126273e-1f,
    9.94082093e-1f, 9.94930983e-1f, 9.95682180e-1f, 9.96344328e-1f, 9.96925533e-1f, 9.97433305e-1f, 9.97874618e-1f, 9.98256087e-1f,
    9.98583674e-1f, 9.98862922e-1f, 9.99099135e-1f, 9.99296963e-1f, 9.99460995e-1f, 9.99595225e-1f, 9.99703407e-1f, 9.99789119e-1f,
    9.99855518e-1f, 9.99905586e-1f, 9.99941945e-1f, 9.99967217e-1f, 9.99983609e-1f, 9.99993265e-1f, 9.99998033e-1f, 9.99999762e-1f
    };

// Here we go...
static const float sin_1[] = {0.7071067811865476};

static const float sin_2[] = {0.3826834323650898,  0.9238795325112867};

static const float sin_3[] = {0.19509032201612825, 0.8314696123025452,  0.9807852804032304, 0.5555702330196022};

static const float sin_4[] = {0.0980171403295606,  0.47139673682599764, 0.7730104533627369, 0.9569403357322089,
                  0.9951847266721969,  0.881921264348355,   0.6343932841636455, 0.2902846772544624};
    
static const float sin_5[] = {0.049067674327418015, 0.24298017990326387, 0.4275550934302821,
                  0.5956993044924334, 0.7409511253549591, 0.8577286100002721, 0.9415440651830208,
                  0.989176509964781,  0.9987954562051724, 0.970031253194544, 0.9039892931234434,
                  0.8032075314806449, 0.6715589548470186, 0.5141027441932218,
                  0.33688985339222033, 0.1467304744553618};

static const float sin_6[] = {
    0.024541228522912288, 0.1224106751992162, 0.2191012401568698,
    0.3136817403988915, 0.40524131400498986, 0.49289819222978404,
    0.5758081914178453, 0.6531728429537768, 0.7242470829514669, 0.7883464276266062,
    0.844853565249707, 0.8932243011955153, 0.9329927988347388, 0.9637760657954398,
    0.9852776423889412, 0.9972904566786902, 0.9996988186962042, 0.99247953459871,
    0.9757021300385286, 0.9495281805930367, 0.9142097557035307, 0.8700869911087115,
    0.8175848131515837, 0.7572088465064847, 0.689540544737067, 0.6152315905806269,
    0.5349976198870972, 0.4496113296546069, 0.35989503653498833,
    0.2667127574748985, 0.17096188876030122, 0.07356456359966773
};

static const float sin_7[] = {
    0.012271538285719925, 0.06132073630220858, 0.11022220729388306,
    0.15885814333386145, 0.20711137619221856, 0.25486565960451457,
    0.3020059493192281, 0.34841868024943456, 0.3939920400610481,
    0.43861623853852766, 0.4821837720791227, 0.524589682678469, 0.5657318107836131,
    0.6055110414043255, 0.6438315428897914, 0.680600997795453, 0.7157308252838186,
    0.7491363945234593, 0.7807372285720945, 0.8104571982525948, 0.838224705554838,
    0.8639728561215867, 0.8876396204028539, 0.9091679830905223, 0.9285060804732156,
    0.9456073253805213, 0.9604305194155658, 0.9729399522055601, 0.9831054874312163,
    0.99090263542778, 0.996312612182778, 0.9993223845883495, 0.9999247018391445,
    0.9981181129001492, 0.9939069700023561, 0.9873014181578584, 0.9783173707196277,
    0.9669764710448521, 0.9533060403541939, 0.937339011912575, 0.9191138516900578,
    0.8986744656939539, 0.8760700941954066, 0.8513551931052652, 0.8245893027850252,
    0.7958369046088836, 0.7651672656224591, 0.7326542716724128, 0.6983762494089729,
    0.662415777590172, 0.6248594881423863, 0.585797857456439, 0.5453249884220464,
    0.5035383837257176, 0.4605387109582402, 0.41642956009763715,
    0.3713171939518377, 0.32531029216226326, 0.27851968938505317,
    0.23105810828067133, 0.1830398879551409, 0.13458070850712628,
    0.08579731234444016, 0.03680722294135883
 };

static const float sin_8[] = {
                0.006135884649154475, 0.030674803176636626, 0.055195244349689934,
                0.07968243797143013, 0.10412163387205459, 0.12849811079379317,
                0.15279718525844344, 0.17700422041214875, 0.2011046348420919,
                0.22508391135979283, 0.24892760574572015, 0.272621355449949,
                0.2961508882436238, 0.3195020308160157, 0.3426607173119944,
                0.36561299780477385, 0.38834504669882625, 0.4108431710579039,
                0.43309381885315196, 0.45508358712634384, 0.4767992300633221,
                0.4982276669727818, 0.5193559901655896, 0.5401714727298929, 0.560661576197336,
                0.5808139580957645, 0.600616479383869, 0.6200572117632891, 0.6391244448637757,
                0.6578066932970786, 0.6760927035753159, 0.6939714608896539, 0.7114321957452164,
                0.7284643904482252, 0.745057785441466, 0.7612023854842618, 0.7768884656732324,
                0.7921065773002123, 0.8068475535437992, 0.8211025149911046, 0.83486287498638,
                0.8481203448032971, 0.8608669386377673, 0.8730949784182901, 0.8847970984309378,
                0.8959662497561851, 0.9065957045149153, 0.9166790599210427, 0.9262102421383113,
                0.9351835099389475, 0.9435934581619604, 0.9514350209690083, 0.9587034748958716,
                0.9653944416976894, 0.9715038909862518, 0.9770281426577544, 0.9819638691095552,
                0.9863080972445987, 0.9900582102622971, 0.9932119492347945, 0.9957674144676598,
                0.9977230666441916, 0.9990777277526454, 0.9998305817958234, 0.9999811752826011,
                0.9995294175010931, 0.9984755805732948, 0.9968202992911658, 0.9945645707342554,
                0.9917097536690995, 0.9882575677307495, 0.984210092386929, 0.9795697656854405,
                0.9743393827855759, 0.9685220942744174, 0.9621214042690416, 0.9551411683057707,
                0.9475855910177412, 0.9394592236021899, 0.9307669610789837, 0.921514039342042,
                0.9117060320054299, 0.901348847046022, 0.890448723244758, 0.8790122264286335,
                0.8670462455156928, 0.8545579883654005, 0.8415549774368984, 0.8280450452577558,
                0.8140363297059485, 0.7995372691079052, 0.7845565971555751, 0.7691033376455796,
                0.7531867990436125, 0.73681656887737, 0.7200025079613818, 0.7027547444572252,
                0.6850836677727004, 0.6669999223036376, 0.6485144010221126, 0.6296382389149272,
                0.6103828062763097, 0.5907597018588742, 0.5707807458869673, 0.5504579729366049,
                0.5298036246862948, 0.5088301425431073, 0.4875501601484359, 0.4659764957679662,
                0.4441221445704293, 0.42200027079979985, 0.39962419984564707,
                0.37700741021641815, 0.3541635254204904, 0.3311063057598765,
                0.30784964004153503, 0.2844075372112721, 0.26079411791527585,
                0.23702360599436717, 0.21311031991609142, 0.18906866414980636,
                0.16491312048997014, 0.14065823933284954, 0.11631863091190471,
                0.09190895649713275, 0.06744391956366418, 0.04293825693494102,
                0.0184067299058051
 };

static const float cos_1[] = {0.7071067811865476};
static const float cos_2[] = {0.9238795325112867, -0.3826834323650897};
static const float cos_3[] = {
    0.9807852804032304, 0.5555702330196023, -0.1950903220161282,
    -0.8314696123025453};
static const float cos_4[] = {
    0.9951847266721969, 0.881921264348355, 0.6343932841636455, 0.29028467725446233,
    -0.09801714032956065, -0.4713967368259977, -0.773010453362737,
    -0.9569403357322088};

static const float cos_5[] = {0.9987954562051724, 0.970031253194544, 0.9039892931234433, 0.8032075314806449,
 0.6715589548470183, 0.5141027441932217, 0.33688985339222005,
 0.14673047445536175, -0.04906767432741801, -0.24298017990326387,
 -0.42755509343028186, -0.5956993044924334, -0.7409511253549589,
 -0.857728610000272, -0.9415440651830207, -0.989176509964781};
static const float cos_6[] = {0.9996988186962042, 0.99247953459871, 0.9757021300385286, 0.9495281805930367,
 0.9142097557035307, 0.8700869911087115, 0.8175848131515837, 0.7572088465064846,
 0.6895405447370669, 0.6152315905806268, 0.5349976198870973, 0.4496113296546066,
 0.3598950365349883, 0.2667127574748984, 0.17096188876030136,
 0.07356456359966745, -0.024541228522912142, -0.12241067519921615,
 -0.21910124015686966, -0.3136817403988914, -0.40524131400498975,
 -0.492898192229784, -0.5758081914178453, -0.6531728429537765,
 -0.7242470829514668, -0.7883464276266062, -0.8448535652497071,
 -0.8932243011955152, -0.9329927988347388, -0.9637760657954398,
 -0.9852776423889412, -0.9972904566786902};
static const float cos_7[] = {0.9999247018391445, 0.9981181129001492, 0.9939069700023561, 0.9873014181578584,
 0.9783173707196277, 0.9669764710448521, 0.9533060403541939, 0.937339011912575,
 0.9191138516900578, 0.8986744656939538, 0.8760700941954066, 0.8513551931052652,
 0.8245893027850253, 0.7958369046088836, 0.765167265622459, 0.7326542716724128,
 0.6983762494089729, 0.6624157775901718, 0.6248594881423865, 0.5857978574564389,
 0.5453249884220465, 0.5035383837257176, 0.46053871095824, 0.4164295600976373,
 0.3713171939518376, 0.325310292162263, 0.27851968938505306,
 0.23105810828067128, 0.18303988795514106, 0.13458070850712622,
 0.08579731234443988, 0.03680722294135899, -0.012271538285719823,
 -0.06132073630220853, -0.11022220729388306, -0.15885814333386128,
 -0.20711137619221845, -0.2548656596045145, -0.3020059493192281,
 -0.3484186802494344, -0.393992040061048, -0.4386162385385274,
 -0.4821837720791227, -0.5245896826784687, -0.5657318107836132,
 -0.6055110414043254, -0.6438315428897913, -0.680600997795453,
 -0.7157308252838186, -0.7491363945234591, -0.7807372285720945,
 -0.8104571982525947, -0.8382247055548381, -0.8639728561215867,
 -0.8876396204028538, -0.9091679830905224, -0.9285060804732155,
 -0.9456073253805212, -0.9604305194155658, -0.9729399522055601,
 -0.9831054874312163, -0.99090263542778, -0.996312612182778,
 -0.9993223845883495};
static const float cos_8[] = {0.9999811752826011, 0.9995294175010931, 0.9984755805732948, 0.9968202992911657,
 0.9945645707342554, 0.9917097536690995, 0.9882575677307495, 0.984210092386929,
 0.9795697656854405, 0.9743393827855759, 0.9685220942744174, 0.9621214042690416,
 0.9551411683057708, 0.9475855910177411, 0.9394592236021899, 0.9307669610789837,
 0.9215140393420419, 0.9117060320054299, 0.901348847046022, 0.8904487232447579,
 0.8790122264286335, 0.8670462455156926, 0.8545579883654005, 0.8415549774368984,
 0.8280450452577558, 0.8140363297059484, 0.799537269107905, 0.7845565971555752,
 0.7691033376455797, 0.7531867990436125, 0.7368165688773698, 0.7200025079613817,
 0.7027547444572253, 0.6850836677727004, 0.6669999223036375, 0.6485144010221126,
 0.6296382389149271, 0.6103828062763095, 0.5907597018588743, 0.5707807458869674,
 0.5504579729366048, 0.5298036246862948, 0.508830142543107, 0.48755016014843605,
 0.4659764957679661, 0.44412214457042926, 0.4220002707997998,
 0.3996241998456468, 0.3770074102164183, 0.3541635254204905,
 0.33110630575987643, 0.307849640041535, 0.2844075372112718,
 0.26079411791527557, 0.23702360599436734, 0.21311031991609136,
 0.18906866414980628, 0.1649131204899701, 0.14065823933284924,
 0.11631863091190488, 0.0919089564971327, 0.0674439195636641,
 0.04293825693494096, 0.01840672990580482, -0.006135884649154393,
 -0.03067480317663646, -0.05519524434968991, -0.07968243797143001,
 -0.1041216338720546, -0.1284981107937931, -0.1527971852584433,
 -0.17700422041214875, -0.20110463484209182, -0.22508391135979267,
 -0.24892760574572012, -0.27262135544994887, -0.29615088824362384,
 -0.31950203081601564, -0.34266071731199427, -0.36561299780477385,
 -0.3883450466988262, -0.4108431710579038, -0.4330938188531519,
 -0.4550835871263437, -0.4767992300633219, -0.4982276669727816,
 -0.5193559901655896, -0.5401714727298929, -0.5606615761973359,
 -0.5808139580957644, -0.6006164793838688, -0.6200572117632892,
 -0.6391244448637757, -0.6578066932970785, -0.6760927035753158,
 -0.6939714608896538, -0.7114321957452165, -0.7284643904482252,
 -0.745057785441466, -0.7612023854842617, -0.7768884656732323,
 -0.7921065773002122, -0.8068475535437993, -0.8211025149911046,
 -0.83486287498638, -0.8481203448032971, -0.8608669386377672,
 -0.8730949784182901, -0.8847970984309378, -0.8959662497561851,
 -0.9065957045149153, -0.9166790599210426, -0.9262102421383114,
 -0.9351835099389476, -0.9435934581619604, -0.9514350209690083,
 -0.9587034748958715, -0.9653944416976893, -0.9715038909862518,
 -0.9770281426577544, -0.9819638691095552, -0.9863080972445986,
 -0.990058210262297, -0.9932119492347945, -0.9957674144676598,
 -0.9977230666441916, -0.9990777277526454, -0.9998305817958234};

static const float* sintables[] = {sin_1, sin_2, sin_3, sin_4, sin_5, sin_6, sin_7, sin_8};
static const float* costables[] = {cos_1, cos_2, cos_3, cos_4, cos_5, cos_6, cos_7, cos_8};

void DCT4(float* input, int subframe, stChannel *ch){
    {
        float* sinTable = (float *)sintables[HCA_MDCT_BITS];
        float* cosTable = (float *)costables[HCA_MDCT_BITS];
        float *dctTemp = &ch->temp[0];

        int size = HCA_SAMPLES_PER_SUBFRAME;
        int lastIndex = size - 1;
        int halfSize = size / 2;

        for (int i = 0; i < halfSize; i++)
        {
            int i2 = i * 2;
            float a = input[i2];
            float b = input[lastIndex - i2];
            float sin = sinTable[i];
            float cos = cosTable[i];
            dctTemp[i2] = a * cos + b * sin;
            dctTemp[i2 + 1] = a * sin - b * cos;
        }
        int stageCount = HCA_MDCT_BITS - 1;

        for (int stage = 0; stage < stageCount; stage++)
        {
            int blockCount = 1 << stage;
            int blockSizeBits = stageCount - stage;
            int blockHalfSizeBits = blockSizeBits - 1;
            int blockSize = 1 << blockSizeBits;
            int blockHalfSize = 1 << blockHalfSizeBits;
            sinTable = (float *)sintables[blockHalfSizeBits];
            cosTable = (float *)costables[blockHalfSizeBits];

            for (int block = 0; block < blockCount; block++)
            {
                for (int i = 0; i < blockHalfSize; i++)
                {
                    int frontPos = (block * blockSize + i) * 2;
                    int backPos = frontPos + blockSize;
                    float a = dctTemp[frontPos] - dctTemp[backPos];
                    float b = dctTemp[frontPos + 1] - dctTemp[backPos + 1];
                    float sin = sinTable[i];
                    float cos = cosTable[i];
                    dctTemp[frontPos] += dctTemp[backPos];
                    dctTemp[frontPos + 1] += dctTemp[backPos + 1];
                    dctTemp[backPos] = a * cos + b * sin;
                    dctTemp[backPos + 1] = a * sin - b * cos;
                }
            }
        }

        for (int i = 0; i < HCA_SAMPLES_PER_SUBFRAME; i++)
        {
            ch->spectra[subframe][i] = dctTemp[ShuffleTables[i]] * 0.125f; // (0.125 is the scale derived by sqrt(2 / 128))
        }
    }
}

// No idea what I am doing here, but I should modify this to MDCT for WAV->HCA, since HCA->WAV has a different method.
static void mdct_transform(stChannel* ch, int subframe) {
    static const unsigned int size = HCA_SAMPLES_PER_SUBFRAME;
    static const unsigned int half = HCA_SAMPLES_PER_SUBFRAME / 2;
    static const unsigned int mdct_bits = HCA_MDCT_BITS;
    unsigned int i, j, k;
    float *scratchmdct = new float[HCA_SAMPLES_PER_SUBFRAME];
    memset(scratchmdct, 0, sizeof(float)*HCA_SAMPLES_PER_SUBFRAME);
    {
        for(int i = 0; i < half; i++){
            // input, in this case, is wave array and output would be the spectra array.
            float a = hcamdct_window_float[half - i - 1] * -ch->wave[subframe][half + i];
            float b = hcamdct_window_float[half + i] * ch->wave[subframe][half- i - 1];
            float c = hcamdct_window_float[i] * ch->imdct_previous[i];
            float d = hcamdct_window_float[size - i - 1] * ch->imdct_previous[size - i - 1];

            scratchmdct[i] = a - b;
            scratchmdct[half + i] = c - d;
        }
    }

    // DCT 4 here.
    DCT4(scratchmdct, subframe, ch);
    // All taken from VGAudio.
    memcpy(ch->imdct_previous, ch->wave[subframe], HCA_SAMPLES_PER_SUBFRAME*sizeof(float));
}

void CalculateLoopInfo(clHCA *hca, int loopStart, int loopEnd){
    loopStart += hca->encoder_delay;
    loopEnd += hca->encoder_delay;

    hca->loop_start_frame = loopStart / HCA_SAMPLES_PER_FRAME;
    hca->loop_start_delay = loopStart % HCA_SAMPLES_PER_FRAME;
    hca->loop_end_frame = loopEnd / HCA_SAMPLES_PER_FRAME;
    hca->loop_end_padding = HCA_SAMPLES_PER_FRAME - loopEnd % HCA_SAMPLES_PER_FRAME;

    if (hca->loop_end_padding == HCA_SAMPLES_PER_FRAME)
    {
        hca->loop_end_frame--;
        hca->loop_end_padding = 0;
    }
}

int CalculateHeaderSize(clHCA *hca){
    const int baseHeaderSize = 96;
    const int baseHeaderAlignment = 32;
    const int loopFrameAlignment = 2048;

    int HeaderSize = GetNextMultiple(baseHeaderSize + hca->comment_len, baseHeaderAlignment);
    if (hca->loop_flag)
    {
        int loopFrameOffset = HeaderSize + hca->frame_size * hca->loop_start_frame;
        int paddingBytes = GetNextMultiple(loopFrameOffset, loopFrameAlignment) - loopFrameOffset;
        int paddingFrames = paddingBytes / hca->frame_size;

        hca->encoder_delay += paddingFrames * HCA_SAMPLES_PER_FRAME;
        hca->loop_start_frame += paddingFrames;
        hca->loop_end_frame += paddingFrames;
        HeaderSize += paddingBytes % hca->frame_size;
    }
    return HeaderSize;
}

static const float IntensityRatioBoundsTable[] = {
    1.9285714285714286, 1.7857142857142858, 1.6428571428571428, 1.5,
    1.3571428571428572, 1.2142857142857142, 1.0714285714285714, 0.9285714285714286,
    0.7857142857142857, 0.6428571428571429, 0.5, 0.35714285714285715,
    0.21428571428571427, 0.07142857142857142
};

// Values here are too big to be precise for a floating point.
// However, most of the res/quality will take values from high indexes where it's a lot more precise.
static const float QuantizerScalingTable[] = {
    6295709.316290409, 4724974.131015034, 3546126.32463723, 2661392.7529765638,
    1997393.983509814, 1499058.2359177016, 1125053.7516509197, 844360.7551570125,
    633698.6866655345, 475595.32229437743, 356937.63510618743, 267884.2061367853,
    201048.98122101015, 150888.67474839764, 113243.01187231517, 84989.67705360873,
    63785.35051523642, 47871.35427971152, 35927.788152959096, 26964.0577540746,
    20236.715031542746, 15187.79699268312, 11398.548486323634, 8554.690825645386,
    6420.355645298913, 4818.522077799559, 3616.334716791347, 2714.0846451911534,
    2036.9396192944837, 1528.737513770257, 1147.3282584674384, 861.0779292198047,
    646.2450434018571, 485.0114512862637, 364.0045061553068, 273.18794257326715,
    205.02947272738496, 153.87606162594616, 115.48506673962403, 86.67235500396121,
    65.04821215430105, 48.819140823825265, 36.63910862181776, 27.497908769959665,
    20.63737397450667, 15.488494347938365, 11.624223966792428, 8.724061861322065,
    6.547469799067861, 4.913922144427483, 3.6879331379172533, 2.767819763927665,
    2.0772682039227583, 1.5590044002378376, 1.1700437696832504, 0.87812608018665,
    0.659039800633032, 0.49461400659698784, 0.37121129146818815, 0.27859668564897316, 
    0.20908877245519958, 0.1569225946280864, 0.11777151118952091, 0.08838834764831843
};

int QuantizedSpectrumMaxBits[] = {0, 2, 3, 3, 4, 4, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12};

void GetChannelTypes(clHCA *Frame, channel_type_t *types){
    int channelsPerTrack = Frame->channels / Frame->track_count;
    if (Frame->stereo_band_count == 0 || channelsPerTrack == 1){
        // types = new channel_type_t[8];
        memset(types, DISCRETE, 8);
        return;
    }

    switch (channelsPerTrack)
    {
        case 2: 
            types[0] = STEREO_PRIMARY; 
            types[1] = STEREO_SECONDARY;
            break;
        case 3: 
            types[0] = STEREO_PRIMARY; 
            types[1] = STEREO_SECONDARY; 
            types[2] = DISCRETE;
            break;
        case 4: 
            if(Frame->channel_config != 0){
                types[0] = STEREO_PRIMARY; 
                types[1] = STEREO_SECONDARY; 
                types[2] = DISCRETE;
                types[3] = DISCRETE;
                }
            else if(Frame->channel_config == 0){
                types[0] = STEREO_PRIMARY; 
                types[1] = STEREO_SECONDARY; 
                types[2] = STEREO_PRIMARY; 
                types[3] = STEREO_SECONDARY;
                }
            break;
        case 5: 
            if(Frame->channel_config > 2){
                types[0] = STEREO_PRIMARY; 
                types[1] = STEREO_SECONDARY; 
                types[2] = DISCRETE; 
                types[3] = DISCRETE; 
                types[4] = DISCRETE;
                }
            else if(Frame->channel_config <= 2){
                types[0] = STEREO_PRIMARY; 
                types[1] = STEREO_SECONDARY; 
                types[2] = DISCRETE; 
                types[3] = STEREO_PRIMARY; 
                types[4] = STEREO_SECONDARY;
                }
            break;
        case 6: 
            types[0] = STEREO_PRIMARY; 
            types[1] = STEREO_SECONDARY; 
            types[2] = DISCRETE; 
            types[3] = DISCRETE; 
            types[4] = STEREO_PRIMARY; 
            types[5] = STEREO_SECONDARY;
            break;
        case 7: 
            types[0] = STEREO_PRIMARY; 
            types[1] = STEREO_SECONDARY; 
            types[2] = DISCRETE; 
            types[3] = DISCRETE; 
            types[4] = STEREO_PRIMARY; 
            types[5] = STEREO_SECONDARY; 
            types[6] = DISCRETE;
            break;
        case 8: 
            types[0] = STEREO_PRIMARY; 
            types[1] = STEREO_SECONDARY; 
            types[2] = DISCRETE; 
            types[3] = DISCRETE; 
            types[4] = STEREO_PRIMARY; 
            types[5] = STEREO_SECONDARY; 
            types[6] = STEREO_PRIMARY;
            types[7] = STEREO_SECONDARY;
            break;
        default: 
            types = new channel_type_t[8];
            memset(types, DISCRETE, 8);
            break;
    }
}

void SetChannelType(clHCA *Frame){
    channel_type_t *types = new channel_type_t[Frame->channels];
    GetChannelTypes(Frame, types);
    stChannel *ch = Frame->channel;
    memset(ch, 0, sizeof(stChannel)*Frame->channels);
    for (int i = 0; i < Frame->channels; i++){
        {
            ch[i].type = types[i],
            ch[i].coded_count = types[i] == STEREO_SECONDARY ? Frame->base_band_count : Frame->base_band_count + Frame->stereo_band_count;
        };
    }
}

void EncodeIntensityStereo(clHCA *frame){
    if (frame->stereo_band_count <= 0) return;

    for (int c = 0; c < frame->channels; c++)
    {
        if (frame->channel[c].type != STEREO_PRIMARY) continue;

        for (int sf = 0; sf < HCA_SUBFRAMES; sf++)
        {
            float *l = &frame->channel[c].spectra[sf][0];
            float *r = &frame->channel[c + 1].spectra[sf][0];

            float energyL = 0;
            float energyR = 0;
            float energyTotal = 0;

            for (int b = frame->base_band_count; b < frame->total_band_count; b++)
            {
                energyL += abs(l[b]);
                energyR += abs(r[b]);
                energyTotal += abs(l[b] + r[b]);
            }
            energyTotal *= 2;

            float energyLR = energyR + energyL;
            float storedValue = 2 * energyL / energyLR;
            float energyRatio = energyLR / energyTotal;
            if(energyRatio < 0.5)energyRatio=0.5;
            else if(energyRatio > sqrt(2) / 2)energyRatio = sqrt(2) / 2;

            int quantized = 1;
            if (energyR > 0 || energyL > 0)
            {
                while (quantized < 13 && IntensityRatioBoundsTable[quantized] >= storedValue)
                {
                    quantized++;
                }
            }
            else
            {
                quantized = 0;
                energyRatio = 1;
            }

            frame->channel[c + 1].intensity[sf] = quantized;

            for (int b = frame->base_band_count; b < frame->total_band_count; b++)
            {
                l[b] = (l[b] + r[b]) * energyRatio;
                r[b] = 0;
            }
        }
    }
}

unsigned char DefaultChannelMapping[] = { 0, 1, 0, 4, 0, 1, 3, 7, 3 };

int FindScaleFactor(float value){
    const float *sf = &hcadequantizer_scaling_table_float[0];
    unsigned int low = 0;
    unsigned int high = 63;
    while (low < high)
    {
        unsigned int mid = (low + high) / 2;
        if (sf[mid] <= value)
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }
    return (int)low;
}

void CalculateHfrGroupAverages(clHCA *frame){
    if (frame->hfr_group_count == 0) return;

    int hfrStartBand = frame->stereo_band_count + frame->base_band_count;
    stChannel *channel = frame->channel;
    for(int c = 0; c < frame->channels; c++)
    {
        if (channel->type == STEREO_SECONDARY) continue;

        for (int group = 0, band = hfrStartBand; group < frame->hfr_group_count; group++)
        {
            float sum = 0.0;
            int count = 0;

            for (int i = 0; i < frame->bands_per_hfr_group && band < HCA_SAMPLES_PER_SUBFRAME; band++, i++)
            {
                for (int subframe = 0; subframe < HCA_SUBFRAMES; subframe++)
                {
                    sum += abs(channel->spectra[subframe][band]);
                }
                count += HCA_SUBFRAMES;
            }

            channel->HfrGroupAverageSpectra[group] = sum / count;
        }
    }
}

void CalculateHfrScale(clHCA *hca){
    if (hca->hfr_group_count == 0) return;

    int hfrStartBand = hca->stereo_band_count + hca->base_band_count;
    int hfrBandCount = std::min(hca->HfrBandCount, hca->total_band_count - hca->HfrBandCount);

    for(int c = 0; c < hca->channels; c++)
    {
        if(hca->channel[c].type == STEREO_SECONDARY) continue;

        float *groupSpectra = &hca->channel[c].HfrGroupAverageSpectra[0];

        for (int group = 0, band = 0; group < hca->hfr_group_count; group++)
        {
            float sum = 0.0;
            int count = 0;

            for (int i = 0; i < hca->bands_per_hfr_group && band < hfrBandCount; band++, i++)
            {
                for (int subframe = 0; subframe < HCA_SUBFRAMES; subframe++)
                {
                    sum += abs(hca->channel[c].scaledspectra[hfrStartBand - band - 1][subframe]);
                }
                count += HCA_SUBFRAMES;
            }

            float averageSpectra = sum / count;
            if (averageSpectra > 0.0)
            {
                groupSpectra[group] *= std::min(1.0 / averageSpectra, sqrt(2));
            }

            hca->channel[c].HfrScales[group] = FindScaleFactor(groupSpectra[group]);
        }
    }
}

void CalculateHfrValues(clHCA *Frame){
    if (Frame->bands_per_hfr_group <= 0) return;

    int HfrBandCount = Frame->total_band_count - Frame->base_band_count - Frame->stereo_band_count;
    Frame->HfrBandCount = DivideByRoundUp(HfrBandCount, Frame->bands_per_hfr_group);
}


void CalculateOptimalDeltaLength(stChannel *channel){
    bool emptyChannel = 1;
    for (int i = 0; i < channel->coded_count; i++)
    {
        if (channel->scalefactors[i] != 0)
        {
            emptyChannel = 0;
            break;
        }
    }

    if (emptyChannel)
    {
        channel->HeaderLengthBits = 3;
        channel->ScaleFactorDeltaBits = 0;
        return;
    }

    int minDeltaBits = 6;
    int minLength = 3 + 6 * channel->coded_count;

    for (int deltaBits = 1; deltaBits < 6; deltaBits++)
    {
        int maxDelta = (1 << (deltaBits - 1)) - 1;
        int length = 3 + 6;
        for (int band = 1; band < channel->coded_count; band++)
        {
            int delta = channel->scalefactors[band] - channel->scalefactors[band - 1];
            length += abs(delta) > maxDelta ? deltaBits + 6 : deltaBits;
        }
        if (length < minLength)
        {
            minLength = length;
            minDeltaBits = deltaBits;
        }
    }

    channel->HeaderLengthBits = minLength;
    channel->ScaleFactorDeltaBits = minDeltaBits;
}

void CalculateFrameHeaderLength(clHCA *frame){
    for(int c = 0; c < frame->channels; c++){
        CalculateOptimalDeltaLength(&frame->channel[c]);
        if (frame->channel[c].type == STEREO_SECONDARY) frame->channel[c].HeaderLengthBits += 32;
        else if (frame->hfr_group_count > 0) frame->channel[c].HeaderLengthBits += 6 * frame->hfr_group_count;
    }
}

static const int ScaleToResolutionCurve[] = {
    15, 14, 14, 14, 14, 14, 14, 13, 13, 13, 13, 13, 13, 12, 12, 12, 12, 12, 12, 11,
    11, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10, 9, 9, 9, 9, 9, 9, 8, 8, 8, 8,
    8, 8, 7, 6, 6, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 2, 1
    };

int CalculateResolution(int scaleFactor, int noiseLevel){
    if (scaleFactor == 0)
    {
        return 0;
    }
    int curvePosition = noiseLevel - 5 * scaleFactor / 2 + 2;
    if(curvePosition < 0){
        curvePosition = 0;
    }else if(curvePosition > 58){
        curvePosition = 58;
    }
    return ScaleToResolutionCurve[curvePosition];
}

// First element of this table in VGAudio's generator is actually NaN, but here I just defaulted it to 0.
static const float QuantizerDeadZone[] = {
    0, 0.3333333333333332, 0.19999999999999993, 0.14285714285714274,
    0.11111111111111104, 0.09090909090909083, 0.07692307692307683,
    0.06666666666666655, 0.03225806451612892, 0.01587301587301576,
    0.007874015748031385, 0.003921568627450869, 0.0019569471624265034,
    0.0009775171065492536, 0.0004885197850511835, 0.0002442002442001332
    };

static const float QuantizerInverseStepSize[] = {
    0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 15.5, 31.5, 63.5, 127.5, 255.5, 511.5, 1023.5, 2047.5
    };

static const int QuantizeSpectrumBits[8][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 3, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 3, 3, 3, 2, 3, 3, 3, 0, 0, 0, 0},
    {0, 0, 0, 0, 4, 3, 3, 3, 3, 3, 3, 3, 4, 0, 0, 0},
    {0, 0, 0, 4, 4, 4, 3, 3, 3, 3, 3, 4, 4, 4, 0, 0},
    {0, 0, 4, 4, 4, 4, 4, 3, 3, 3, 4, 4, 4, 4, 4, 0},
    {0, 4, 4, 4, 4, 4, 4, 4, 3, 4, 4, 4, 4, 4, 4, 4}
};

int CalculateUsedBits(clHCA *frame, int noiseLevel, int evalBoundary){
    int length = 16 + 16 + 16; // Sync word, noise level and checksum

    for(int c = 0; c < frame->channels; c++)
    {
        length += frame->channel[c].HeaderLengthBits;
        for (int i = 0; i < frame->channel[c].coded_count; i++)
        {
            int noise = i < evalBoundary ? noiseLevel - 1 : noiseLevel;
            int resolution = CalculateResolution(frame->channel[c].scalefactors[i], noise);

            if (resolution >= 8)
            {
                int bits = QuantizedSpectrumMaxBits[resolution] - 1;
                float deadZone = QuantizerDeadZone[resolution];
                for(int j = 0; j < HCA_SUBFRAMES; j++)
                {
                    length += bits;
                    if (abs(frame->channel[c].scaledspectra[i][j]) >= deadZone) length++;
                }
            }
            else
            {
                float stepSizeInv = QuantizerInverseStepSize[resolution];
                float shiftUp = stepSizeInv + 1;
                int shiftDown = (int)(stepSizeInv + 0.5 - 8);
                for(int j = 0; j < HCA_SUBFRAMES; j++)
                {
                    int quantizedSpectra = (int)(frame->channel[c].scaledspectra[i][j] * stepSizeInv + shiftUp) - shiftDown;
                    length += QuantizeSpectrumBits[resolution][quantizedSpectra];
                }
            }
        }
    }

    return length;
}

int BinarySearchLevel(clHCA *frame, int availableBits, int low, int high){
    int max = high;
    int midValue = 0;

    while (low != high)
    {
        int mid = (low + high) / 2;
        midValue = CalculateUsedBits(frame, mid, 0);

        if (midValue > availableBits)
        {
            low = mid + 1;
        }
        else if (midValue <= availableBits)
        {
            high = mid;
        }
    }

    return low == max && midValue > availableBits ? -1 : low;
}

void CalculateNoiseLevel(clHCA *frame){
    int highestBand = frame->base_band_count + frame->stereo_band_count - 1;
    int availableBits = frame->frame_size * 8;
    int maxLevel = 255;
    int minLevel = 0;
    int level = BinarySearchLevel(frame, availableBits, minLevel, maxLevel);

    // If there aren't enough available bits, remove bands until there are.
    while (level < 0)
    {
        highestBand -= 2;
        if (highestBand < 0)
        {
            // errors here
            // return py_decode_err(-3);
            return;
        }

        for(int c = 0; c < frame->channels; c++)
        {
            frame->channel[c].scalefactors[highestBand + 1] = 0;
            frame->channel[c].scalefactors[highestBand + 2] = 0;
        }

        CalculateFrameHeaderLength(frame);
        level = BinarySearchLevel(frame, availableBits, minLevel, maxLevel);
    }

    frame->AcceptableNoiseLevel = level;
}

int BinarySearchBoundary(clHCA *frame, int availableBits, int noiseLevel, int low, int high){
    int max = high;

    while (abs(high - low) > 1)
    {
        int mid = (low + high) / 2;
        int midValue = CalculateUsedBits(frame, noiseLevel, mid);

        if (availableBits < midValue)
        {
            high = mid - 1;
        }
        else if (availableBits >= midValue)
        {
            low = mid;
        }
    }

    if (low == high)
    {
        return low < max ? low : -1;
    }

    int hiValue = CalculateUsedBits(frame, noiseLevel, high);

    return hiValue > availableBits ? low : high;
}

void CalculateEvaluationBoundary(clHCA *frame){
    if (frame->AcceptableNoiseLevel == 0)
    {
        frame->EvaluationBoundary = 0;
        return;
    }

    int availableBits = frame->frame_size * 8;
    int maxLevel = 127;
    int minLevel = 0;
    int level = BinarySearchBoundary(frame, availableBits, frame->AcceptableNoiseLevel, minLevel, maxLevel);
    if(level < 0){
        // errors here
        py_decode_err(-4);
        return;
    }
    frame->EvaluationBoundary = level;
}

void CalculateFrameResolutions(clHCA *frame){
    for(int c = 0; c < frame->channels; c++)
    {
        for (int i = 0; i < frame->EvaluationBoundary; i++)
        {
            frame->channel[c].resolution[i] = CalculateResolution(frame->channel[c].scalefactors[i], frame->AcceptableNoiseLevel - 1);
        }
        for (int i = frame->EvaluationBoundary; i < frame->channel[c].coded_count; i++)
        {
            frame->channel[c].resolution[i] = CalculateResolution(frame->channel[c].scalefactors[i], frame->AcceptableNoiseLevel);
        }
        memset(frame->channel[c].resolution+frame->channel[c].coded_count, 0, HCA_SAMPLES_PER_SUBFRAME-frame->channel[c].coded_count);
    }
}

void QuantizeSpectra(clHCA *frame){
    for(int c = 0; c < frame->channels; c++)
    {
        for (int i = 0; i < frame->channel[c].coded_count; i++)
        {
            float *scaled = &frame->channel[c].scaledspectra[i][0];
            int resolution = frame->channel[c].resolution[i];
            float stepSizeInv = QuantizerInverseStepSize[resolution];
            float shiftUp = stepSizeInv + 1;
            int shiftDown = (int)(stepSizeInv + 0.5);

            for (int sf = 0; sf < HCA_SUBFRAMES; sf++)
            {
                int quantizedSpectra = (int)(scaled[sf] * stepSizeInv + shiftUp) - shiftDown;
                frame->channel[c].QuantizedSpectra[sf][i] = quantizedSpectra;
            }
        }
    }
}

// Packing functions.

void PackHeader(BitWriter &outfile, clHCA *Frame, int header_size){
    static const unsigned int HCAHeader = 0x48434100;
    static const unsigned int FMTHeader = 0x666D7400;
    static const unsigned int COMHeader = 0x636F6D70;
    static const unsigned int LOPHeader = 0x6C6F6F70;
    static const unsigned int CIPHeader = 0x63697068;
    static const unsigned int PADHeader = 0x70616400;

    // HCA
    outfile.Write(HCAHeader, 32);
    outfile.Write(0x0200, 16); // Version.
    outfile.Write(header_size, 16);

    // FMT
    outfile.Write(FMTHeader, 32);
    outfile.Write(Frame->channels, 8);
    outfile.Write(Frame->sample_rate, 24);
    outfile.Write(Frame->frame_count, 32);
    outfile.Write(Frame->encoder_delay, 16);
    outfile.Write(Frame->encoder_padding, 16);

    // COMP
    outfile.Write(COMHeader, 32);
    outfile.Write(Frame->frame_size, 16);
    outfile.Write(Frame->min_resolution, 8);
    outfile.Write(Frame->max_resolution, 8);
    outfile.Write(Frame->track_count, 8);
    outfile.Write(Frame->channel_config, 8);
    outfile.Write(Frame->total_band_count, 8);
    outfile.Write(Frame->base_band_count, 8);
    outfile.Write(Frame->stereo_band_count, 8);
    outfile.Write(Frame->bands_per_hfr_group, 8);
    outfile.Write(0x0000, 16);

    // LOOP
    if(Frame->loop_flag){
        outfile.Write(LOPHeader, 32);
        outfile.Write(Frame->loop_start_frame, 32);
        outfile.Write(Frame->loop_end_frame, 32);
        outfile.Write(Frame->loop_start_delay, 16);
        outfile.Write(Frame->loop_end_padding, 16);
    }

    // CIPH
    outfile.Write(CIPHeader, 32);
    outfile.Write(0x0000, 16); // No support for encryption now. Should be easy to implement tho.

    // PAD
    outfile.Write(PADHeader, 32);
    if(Frame->loop_flag){
        outfile.pos += (0xCC * 8);
    }else{
        outfile.pos += (0x2C * 8); // Just before the CRC sum.
    }
    outfile.Write(crc16_checksum(outfile.buffer, header_size-2), 16);
}

void WriteScalesFactors(clHCA *Frame, BitWriter &outfile, int c){
    int deltabits = Frame->channel[c].ScaleFactorDeltaBits;
    unsigned char *scales = &Frame->channel[c].scalefactors[0];
    outfile.Write(deltabits, 3);
    if(deltabits ==0)return;
    if(deltabits == 6){
        for (int i = 0; i < Frame->channel[c].coded_count; i++){
            outfile.Write(scales[i], 6);
        }
        return;
    }
    outfile.Write(scales[0], 6);
    int maxDelta = (1 << (deltabits - 1)) - 1;
    int escapeValue = (1 << deltabits) - 1;

    for (int i = 1; i < Frame->channel[c].coded_count; i++){
        int delta = scales[i] - scales[i - 1];
        if (abs(delta) > maxDelta)
        {
            outfile.Write(escapeValue, deltabits);
            outfile.Write(scales[i], 6);
        }
        else
        {
            outfile.Write(maxDelta + delta, deltabits);
        }
    }
}

static const signed char QuantizeSpectrumValue[8][16] = {
    {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
    {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
    {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x7, 0x2, 0x0, 0x1, 0x6, 0x0, 0x0, 0x0, 0x0, 0x0},
    {0x0, 0x0, 0x0, 0x0, 0x0, 0x7, 0x5, 0x3, 0x0, 0x2, 0x4, 0x6, 0x0, 0x0, 0x0, 0x0},
    {0x0, 0x0, 0x0, 0x0, 0xF, 0x6, 0x4, 0x2, 0x0, 0x1, 0x3, 0x5, 0xE, 0x0, 0x0, 0x0},
    {0x0, 0x0, 0x0, 0xF, 0xD, 0xB, 0x4, 0x2, 0x0, 0x1, 0x3, 0xA, 0xC, 0xE, 0x0, 0x0},
    {0x0, 0x0, 0xF, 0xD, 0xB, 0x9, 0x7, 0x2, 0x0, 0x1, 0x6, 0x8, 0xA, 0xC, 0xE, 0x0},
    {0x0, 0xF, 0xD, 0xB, 0x9, 0x7, 0x5, 0x3, 0x0, 0x2, 0x4, 0x6, 0x8, 0xA, 0xC, 0xE}
};

void WriteSpectra(BitWriter &outfile, clHCA *Frame, int sf, int c){
    for(int i = 0; i < Frame->channel[c].coded_count; i++){
        int resolution = Frame->channel[c].resolution[i];
        int quantizedSpectra = Frame->channel[c].QuantizedSpectra[sf][i];
        if (resolution == 0)continue;
        if (resolution < 8){
            int bits = QuantizeSpectrumBits[resolution][quantizedSpectra + 8];
            outfile.Write(QuantizeSpectrumValue[resolution][quantizedSpectra + 8], bits);
        }
        else if (resolution < 16){
            int bits = QuantizedSpectrumMaxBits[resolution] - 1;
            outfile.Write(abs(quantizedSpectra), bits);
            if (quantizedSpectra != 0){
                outfile.Write(quantizedSpectra > 0 ? 0 : 1, 1);
            }
        }
    }
}

void PackFrame(clHCA *Frame, BitWriter &outfile){
    int ogpos = outfile.pos;
    outfile.Write(0xffff, 16);
    outfile.Write(Frame->AcceptableNoiseLevel, 9);
    outfile.Write(Frame->EvaluationBoundary, 7);

    for(int c = 0; c < Frame->channels; c++){
        WriteScalesFactors(Frame, outfile, c);
        if (Frame->channel[c].type == STEREO_SECONDARY){
            for (int i = 0; i < HCA_SUBFRAMES; i++){
                outfile.Write(Frame->channel[c].intensity[i], 4);
            }
        }
        else if (Frame->hfr_group_count > 0){
            for (int i = 0; i < Frame->hfr_group_count; i++){
                outfile.Write(Frame->channel[c].HfrScales[i], 6);
            }
        }
    }

    for (int sf = 0; sf < HCA_SUBFRAMES; sf++){
        for(int c = 0; c < Frame->channels; c++){
            WriteSpectra(outfile, Frame, sf, c);
        }
    }

    outfile.AlignPosition(8);
    for (int i = outfile.pos / 8; i < Frame->frame_size - 2; i++){
        outfile.buffer[i] = 0;
    }

    outfile.pos = ogpos + (Frame->frame_size * 8) - 16;
    unsigned short crc16 = crc16_checksum(outfile.buffer+(outfile.pos/8)-(Frame->frame_size - 2), Frame->frame_size - 2);
    outfile.Write(crc16, 16);
}

// Insanely messy.
static PyObject* HcaEncode(PyObject* self, PyObject* args){
    unsigned char *data;
    unsigned int data_size;
    unsigned int force_nolooping;
    int looping = 0;
    if(!PyArg_ParseTuple(args, "y#I", &data, &data_size, &force_nolooping)){
        return NULL;
    }

    // Initial check is done python side.
    stWAVEHeader fmtdata = *(stWAVEHeader*)data;data+=sizeof(stWAVEHeader);
    // Wav in order is RIFF -> FMT -> SMPL -> NOTE -> DATA, above RIFF and FMT is done.
    // There might be a cue chunk sometimes for 6 or 8 channel audio, unsupported.

    // I didn't count for endiannes so I will check it this way for compatibility.
    // 73 6D 70 6C = smpl
    stWAVEsmpl smplchk;
    if(*data == 0x73 && *(data + 1) == 0x6D && *(data + 2) == 0x70 && *(data + 3) == 0x6C){
        smplchk = *(stWAVEsmpl*)data;data+=sizeof(stWAVEsmpl);
        looping = 1;
    }
    // 6E 6F 74 65 = note
    if(*data == 0x6E && *(data + 1) == 0x6F && *(data + 2) == 0x74 && *(data + 3) == 0x65){
        stWAVEnote notechk = *(stWAVEnote*)data;data+=sizeof(stWAVEnote);
        unsigned char *comment = new unsigned char [notechk.noteSize];
        memcpy(comment, data, notechk.noteSize);
        data += notechk.noteSize; // +1? also +padding_size?
    }
    // 64 61 74 61 = data
    stWAVEdata datachk;
    if(*data == 0x64 && *(data + 1) == 0x61 && *(data + 2) == 0x74 && *(data + 3) == 0x61){
        datachk = *(stWAVEdata*)data;data+=sizeof(stWAVEdata);
    }else{
        // errors here since no data chunk is present.
        return py_decode_err(-5);
    }
    
    int CutoffFrequency = fmtdata.fmtSamplingRate / 2;
    const int PostSamples = 128;

    clHCA *Frame = new clHCA;
    // memsetting just incase.
    memset(Frame, 0, sizeof(clHCA));

    // Initialize values from WAV paramaters.
    {
        Frame->channels = fmtdata.fmtChannelCount;
        Frame->track_count = 1;
        Frame->sample_rate = fmtdata.fmtSamplingRate;
        Frame->min_resolution = 1;
        Frame->max_resolution = 15;
        Frame->encoder_delay = HCA_SAMPLES_PER_SUBFRAME;
    }
    //

    // Bitrate, compression set to HCA default of High. Don't think it's worth supporting other qualities.
    int pcmBitrate = Frame->sample_rate * Frame->channels * 16;
    int Bitrate = pcmBitrate / 6;
    if(Bitrate < 0)Bitrate = 0;
    else if(Bitrate > (pcmBitrate/4))Bitrate = pcmBitrate/4; // Won't happen.
    //

    //////////////////////////////////////////////////////
    // Calc Band counts. Whatever that is.
    //////////////////////////////////////////////////////
    Frame->frame_size = Bitrate * 1024 / Frame->sample_rate / 8;
    int numGroups = 0;
    int hfrRatio; // HFR is used at bitrates below (pcmBitrate / hfrRatio)
    int cutoffRatio; // The cutoff frequency is lowered at bitrates below (pcmBitrate / cutoffRatio)

    if (Frame->channels <= 1 || pcmBitrate / Bitrate <= 6)
    {
        hfrRatio = 6;
        cutoffRatio = 12;
    }
    else
    {
        hfrRatio = 8;
        cutoffRatio = 16;
    }

    if (Bitrate < pcmBitrate / cutoffRatio)
    {
        float CutoffFrequency = std::min((float)CutoffFrequency, (float)(cutoffRatio * Bitrate / (32 * Frame->channels)));
    }

    int totalBandCount = (int)round(CutoffFrequency * 256.0 / Frame->sample_rate);

    int hfrStartBand = (int)std::min((float)totalBandCount, (float)round((float)(hfrRatio * Bitrate * 128.0f) / (float)pcmBitrate));
    int stereoStartBand = hfrRatio == 6 ? hfrStartBand : (hfrStartBand + 1) / 2;

    int hfrBandCount = totalBandCount - hfrStartBand;
    int bandsPerGroup = DivideByRoundUp(hfrBandCount, 8);

    if (bandsPerGroup > 0)
    {
        numGroups = DivideByRoundUp(hfrBandCount, bandsPerGroup);
    }

    Frame->total_band_count = totalBandCount;
    Frame->base_band_count = stereoStartBand;
    Frame->stereo_band_count = hfrStartBand - stereoStartBand;
    Frame->hfr_group_count = numGroups;
    Frame->bands_per_hfr_group = bandsPerGroup;
    //

    //////////////////////////////////////////////////////
    // Init Channel Config.
    //////////////////////////////////////////////////////
    int channelsPerTrack = Frame->channels / Frame->track_count;
    int channelConfig = DefaultChannelMapping[channelsPerTrack];
    // Could be wrong, reason being, I've got no clue how this is derived.
    Frame->channel_config = channelConfig;
    ////

    int inputSampleCount = datachk.dataSize / fmtdata.fmtSamplingSize;

    if(force_nolooping){
        looping = 0;
    }
    if (looping)
    {
        Frame->loop_flag = 1;
        int SampleCount = std::min((int)smplchk.loop_End, inputSampleCount);
        Frame->encoder_delay += GetNextMultiple(smplchk.loop_Start, HCA_SAMPLES_PER_FRAME) - smplchk.loop_Start;
        CalculateLoopInfo(Frame, smplchk.loop_Start, smplchk.loop_End);
        inputSampleCount = std::min(GetNextMultiple(SampleCount, HCA_SAMPLES_PER_SUBFRAME), inputSampleCount);
        inputSampleCount += HCA_SAMPLES_PER_SUBFRAME * 2;
        const int PostSamples = inputSampleCount - (SampleCount);
    }

    int header_size = CalculateHeaderSize(Frame);

    int totalSamples = inputSampleCount + Frame->encoder_delay;

    Frame->frame_count = DivideByRoundUp(totalSamples, HCA_SAMPLES_PER_FRAME);
    Frame->encoder_padding = Frame->frame_count * HCA_SAMPLES_PER_FRAME - Frame->encoder_delay - inputSampleCount;
    int BufferPreSamples = Frame->encoder_delay - HCA_SAMPLES_PER_SUBFRAME;
    CalculateHfrValues(Frame);
    SetChannelType(Frame);

    // Frame(s) are now ready to be encoded.

    /** Unused
    short PostAudio[fmtdata.fmtChannelCount][PostSamples];
    short **PostAudio = new short*[fmtdata.fmtChannelCount];
    for(int i = 0; i < fmtdata.fmtChannelCount; i++){
        PostAudio[i] = new short [PostSamples];
    }
    memset(*PostAudio, 0, fmtdata.fmtChannelCount*sizeof(short)*PostSamples);
    */

    // According to VGAudio, the PCM data must be in a jagged array of [channelcount][1024], and that is then encoded into a single HCA frame.
    short *pcm = new short[fmtdata.fmtChannelCount * 1024];
    memset(pcm, 0, fmtdata.fmtChannelCount*sizeof(short)*1024);
    unsigned int padded_wav_size = datachk.dataSize + (fmtdata.fmtChannelCount*sizeof(short)*1024);
    if(padded_wav_size % (1024*fmtdata.fmtChannelCount) != 0){
        padded_wav_size = (padded_wav_size + (fmtdata.fmtChannelCount*1024 - (padded_wav_size % fmtdata.fmtChannelCount*1024))) + fmtdata.fmtChannelCount*1024;
    }
    short *wavfile = new short[(padded_wav_size)/2];
    memset(wavfile, 0, padded_wav_size); // To make sure our padding was 0's;
    memcpy(wavfile, data, datachk.dataSize);
    unsigned char *hcaOut = new unsigned char[Frame->frame_count*Frame->frame_size + (header_size)];
    memset(hcaOut, 0, Frame->frame_count*Frame->frame_size + (header_size));
    BitWriter outfile;
    outfile.pos = 0;
    outfile.len = (Frame->frame_count*Frame->frame_size + (header_size)) * 8;
    outfile.buffer = hcaOut;
    PackHeader(outfile, Frame, header_size);

    int LoopStartSample = Frame->loop_start_frame * 1024 + Frame->encoder_delay - 128;
    int LoopEndSample = (Frame->loop_end_frame + 1) * 1024 - Frame->encoder_padding - 128;

    // Start going through the WAV and copy the audio to buffer.
    // It's good to note that I have not encoded Post Audio Samples for looping.
    // However, I have no idea how this still works. I might be short on suitable WAV's for testing.
    for(int counter = 0; counter < Frame->frame_count; counter++){
        int smaplesread = counter * HCA_SAMPLES_PER_FRAME;

        if(BufferPreSamples > 0){
            int BufferPosition; // Unused variable.
            while (BufferPreSamples > HCA_SAMPLES_PER_FRAME){
                BufferPosition = HCA_SAMPLES_PER_FRAME;
                PackFrame(Frame, outfile); // This is probably wrong.
                counter++;
                BufferPreSamples -= HCA_SAMPLES_PER_FRAME;
            }
            BufferPosition = BufferPreSamples;
            BufferPreSamples = 0;
        }

        // Save Post Loop Point Audio to PostAudio. (I think?)
        // Although not used at all but somehow works.
        /*
        if(Frame->loop_flag && LoopStartSample >= smaplesread && LoopStartSample < smaplesread + HCA_SAMPLES_PER_FRAME){
            int startPos = std::max(LoopStartSample - smaplesread, 0);
            int loopPos = std::max(smaplesread - LoopStartSample, 0);
            int endPos = std::min(LoopStartSample - smaplesread + (int)PostSamples, smaplesread);
            int length = endPos - startPos;
            for (int i = 0; i < Frame->channels; i++)
            {
                std::copy(&pcm[i][0]+startPos, &pcm[i][0]+startPos+length, &PostAudio[i][0]+loopPos);
            }
        }
        */

        for(int i = 0; i<fmtdata.fmtChannelCount; i++){
            for(int j = 0; j<1024; j++){
                pcm[1024 * i + j] = wavfile[j * fmtdata.fmtChannelCount];
            }
            wavfile++;
        }
        wavfile -= fmtdata.fmtChannelCount;
        wavfile += (1024 * fmtdata.fmtChannelCount);

        /////////////////////////////////////////////////////
        ///// Encoding steps from down here.
        /////////////////////////////////////////////////////

        // PCM to float.
        for(int c = 0; c < fmtdata.fmtChannelCount; c++){
            unsigned int idx = 0;
            for (int sf = 0; sf < HCA_SUBFRAMES; sf++)
            {
                for (int i = 0; i < HCA_SAMPLES_PER_SUBFRAME; i++)
                {
                    Frame->channel[c].wave[sf][i] = (float)(pcm[c * 1024 + idx++] * (float)(1.0f / 32768.0f));
                }
            }
        }

        // Run MDCT.
        for(int c = 0; c < fmtdata.fmtChannelCount; c++){
            for (int sf = 0; sf < HCA_SUBFRAMES; sf++){
                mdct_transform(&Frame->channel[c], sf);
            }
        }

        // Encode Intensity.
        EncodeIntensityStereo(Frame);

        // Calculate Scale Factors.
        for(int c = 0; c < fmtdata.fmtChannelCount; c++){
            for (int b = 0; b < Frame->channel[c].coded_count; b++)
            {
                float max = 0;
                for (int sf = 0; sf < HCA_SUBFRAMES; sf++)
                {
                    float coeff = abs(Frame->channel[c].spectra[sf][b]);
                    max = std::max(coeff, max);
                }
                Frame->channel[c].scalefactors[b] = FindScaleFactor(max);
            }
            memset(Frame->channel[c].scalefactors+Frame->channel[c].coded_count, 0, HCA_SAMPLES_PER_SUBFRAME - Frame->channel[c].coded_count);
        }

        // Scale Spectra.
        for(int c = 0; c < fmtdata.fmtChannelCount; c++){
            for (int b = 0; b < Frame->channel[c].coded_count; b++){
                float *scaledSpectra = &Frame->channel[c].scaledspectra[b][0];
                int scaleFactor = Frame->channel[c].scalefactors[b];
                for (int sf = 0; sf < HCA_SUBFRAMES; sf++){
                    float coeff = Frame->channel[c].spectra[sf][b];
                    float ans = (coeff * QuantizerScalingTable[scaleFactor]);
                    // Clamping.
                    if(ans > 0.9999999f){
                        ans = 0.9999999f;
                    }else if(ans < -0.9999999f){
                        ans = -0.9999999f;
                    }
                    scaledSpectra[sf] = scaleFactor == 0 ? 0 : ans;
                }
            }
        }

        CalculateHfrGroupAverages(Frame);
        CalculateHfrScale(Frame);
        CalculateFrameHeaderLength(Frame);
        CalculateNoiseLevel(Frame);
        CalculateEvaluationBoundary(Frame);
        CalculateFrameResolutions(Frame);
        QuantizeSpectra(Frame);
        PackFrame(Frame, outfile);
    }
    unsigned int outsize = Frame->frame_count*Frame->frame_size + (header_size);
    free(Frame);
    return Py_BuildValue("y#", hcaOut, outsize);
}

// code here is a mess.
static PyObject* HcaDecode(PyObject* self, PyObject* args){
    // Taken from Nyagamon's clHCA code.
    stWAVEHeader wavRiff = { 'R','I','F','F',0,'W','A','V','E','f','m','t',' ',0x10,0,0,0,0,0,0 }; // Riff header.
    stWAVEsmpl wavSmpl = { 's','m','p','l',0x3C,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0 }; // Smpl chunk.
    stWAVEnote wavNote = { 'n','o','t','e',0,0 }; // HCA Comment.
    stWAVEdata wavData = { 'd','a','t','a',0 }; // Sample out buffer.

    unsigned char *data;
    unsigned int data_size;
    unsigned int header_size;
    unsigned long long keycode;
    unsigned short subkey;
    if(!PyArg_ParseTuple(args, "y#IKH", &data, &data_size, &header_size, &keycode, &subkey)){
        return NULL;
    }
    clHCA* hca = (clHCA*)malloc(sizeof(clHCA));
    if(clHCA_DecodeHeader(hca, data, header_size) < 0){
        // header error.
        return py_decode_err(-1);
    }
    // build wav header.
    int mode = 16; // always 16 bits?
    wavRiff.fmtType = (mode>0) ? 1 : 3;
    wavRiff.fmtChannelCount = hca->channels;
    wavRiff.fmtBitCount = (mode>0) ? mode : 32;
    wavRiff.fmtSamplingRate = hca->sample_rate;
    wavRiff.fmtSamplingSize = wavRiff.fmtBitCount / 8 * wavRiff.fmtChannelCount;
    wavRiff.fmtSamplesPerSec = wavRiff.fmtSamplingRate*wavRiff.fmtSamplingSize;

    // set smpl chunk looping info.
    if(hca->loop_flag){
        // wavSmpl.samplePeriod = (unsigned int)(1 / (double)wavRiff.fmtSamplingRate * 1000000000); // Nyagamon used this, seems to have no use tho.
        wavSmpl.loop_Start = hca->loop_start_frame * 0x80 * 8 + hca->encoder_padding;
        wavSmpl.loop_End = (hca->loop_end_frame + 1) * 0x80 * 8 - 1;
        wavSmpl.loop_PlayCount = (hca->loop_start_delay == 0x80) ? 0 : hca->loop_start_delay;
    }
    // Wav note if HCA has a comment. Although we could just discard it.
    if(hca->comment_len){
        wavNote.noteSize = 4 + hca->comment_len + 1;
        if (wavNote.noteSize & 3){ // no clue why is this here.
            wavNote.noteSize += 4 - (wavNote.noteSize & 3);
        }
    }
    wavData.dataSize = hca->frame_count * 0x80 * 8 * wavRiff.fmtSamplingSize;
    wavRiff.riffSize = 0x24 + ((hca->loop_flag) ? sizeof(wavSmpl) : 0) + (hca->comment_len ? 8 + wavNote.noteSize : 0) + sizeof(wavData) + wavData.dataSize;
    data += header_size;
    if(subkey){
        keycode = keycode * ( ((uint64_t)subkey << 16u) | ((uint16_t)~subkey + 2u) );
    }
    clHCA_SetKey(hca, keycode);
    unsigned char *buf = new unsigned char [hca->frame_size];
    signed short *outbuf = new signed short [wavRiff.riffSize]; // wav bytes.
    memcpy(outbuf, &wavRiff, sizeof(stWAVEHeader));outbuf+=(sizeof(stWAVEHeader)/2);
    if(hca->loop_flag){
        memcpy(outbuf, &wavSmpl, sizeof(stWAVEsmpl));
        outbuf+=(sizeof(stWAVEsmpl)/2);
    }
    if(hca->comment_len){
        memcpy(outbuf, &wavNote, sizeof(stWAVEnote));
        outbuf+=(sizeof(stWAVEnote)/2);
        memcpy(outbuf, hca->comment, hca->comment_len);
        outbuf+=(hca->comment_len/2);
    }
    memcpy(outbuf, &wavData, sizeof(stWAVEdata));outbuf+=(sizeof(stWAVEdata)/2);
    int res = 0;
    memset(buf, 0, hca->frame_size);
    int count = 0;
    for(unsigned int i = hca->frame_count*hca->frame_size; i > 0; i -= hca->frame_size, data += hca->frame_size){
        memcpy(buf, data, hca->frame_size);
        res = clHCA_DecodeBlock(hca, buf, hca->frame_size);
        if(res < 0){
            // decode error. most likely wrong key.
            return py_decode_err(-2);
        }
        clHCA_ReadSamples16(hca, outbuf);
    }
    outbuf -= (wavRiff.riffSize/2);
    free(hca);
    return Py_BuildValue("y#", outbuf, wavRiff.riffSize);
}