// NanoJPEG -- KeyJ's Tiny Baseline JPEG Decoder
// version 1.3.5 (2016-11-14)
// Copyright (c) 2009-2016 Martin J. Fiedler <martin.fiedler@gmx.net>
// published under the terms of the MIT license
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.


///////////////////////////////////////////////////////////////////////////////
// DOCUMENTATION SECTION                                                     //
// read this if you want to know what this is all about                      //
///////////////////////////////////////////////////////////////////////////////

// INTRODUCTION
// ============
//
// This is a minimal decoder for baseline JPEG images. It accepts memory dumps
// of JPEG files as input and generates either 8-bit grayscale or packed 24-bit
// RGB images as output. It does not parse JFIF or Exif headers; all JPEG files
// are assumed to be either grayscale or YCbCr. CMYK or other color spaces are
// not supported. All YCbCr subsampling schemes with power-of-two ratios are
// supported, as are restart intervals. Progressive or lossless JPEG is not
// supported.
// Summed up, NanoJPEG should be able to decode all images from digital cameras
// and most common forms of other non-progressive JPEG images.
// The decoder is not optimized for speed, it's optimized for simplicity and
// small code. Image quality should be at a reasonable level. A bicubic chroma
// upsampling filter ensures that subsampled YCbCr images are rendered in
// decent quality. The decoder is not meant to deal with broken JPEG files in
// a graceful manner; if anything is wrong with the bitstream, decoding will
// simply fail.
// The code should work with every modern C compiler without problems and
// should not emit any warnings. It uses only (at least) 32-bit integer
// arithmetic and is supposed to be endianness independent and 64-bit clean.
// However, it is not thread-safe.


// COMPILE-TIME CONFIGURATION
// ==========================
//
// The following aspects of NanoJPEG can be controlled with preprocessor
// defines:
//
// _NJ_EXAMPLE_PROGRAM     = Compile a main() function with an example
//                           program.
// _NJ_INCLUDE_HEADER_ONLY = Don't compile anything, just act as a header
//                           file for NanoJPEG. Example:
//                               #define _NJ_INCLUDE_HEADER_ONLY
//                               #include "nanojpeg.c"
//                               int main(void) {
//                                   njInit();
//                                   // your code here
//                                   njDone();
//                               }
// NJ_USE_LIBC=1           = Use the malloc(), free(), memset() and memcpy()
//                           functions from the standard C library (default).
// NJ_USE_LIBC=0           = Don't use the standard C library. In this mode,
//                           external functions njAlloc(), njFreeMem(),
//                           njFillMem() and njCopyMem() need to be defined
//                           and implemented somewhere.
// NJ_USE_WIN32=0          = Normal mode (default).
// NJ_USE_WIN32=1          = If compiling with MSVC for Win32 and
//                           NJ_USE_LIBC=0, NanoJPEG will use its own
//                           implementations of the required C library
//                           functions (default if compiling with MSVC and
//                           NJ_USE_LIBC=0).
// NJ_CHROMA_FILTER=1      = Use the bicubic chroma upsampling filter
//                           (default).
// NJ_CHROMA_FILTER=0      = Use simple pixel repetition for chroma upsampling
//                           (bad quality, but faster and less code).


// API
// ===
//
// For API documentation, read the "header section" below.


// EXAMPLE
// =======
//
// A few pages below, you can find an example program that uses NanoJPEG to
// convert JPEG files into PGM or PPM. To compile it, use something like
//     gcc -O3 -D_NJ_EXAMPLE_PROGRAM -o nanojpeg nanojpeg.c
// You may also add -std=c99 -Wall -Wextra -pedantic -Werror, if you want :)
// The only thing you might need is -Wno-shift-negative-value, because this
// code relies on the target machine using two's complement arithmetic, but
// the C standard does not, even though *any* practically useful machine
// nowadays uses two's complement.


///////////////////////////////////////////////////////////////////////////////
// HEADER SECTION                                                            //
// copy and pase this into nanojpeg.h if you want                            //
///////////////////////////////////////////////////////////////////////////////

#ifndef _NANOJPEG_H
#define _NANOJPEG_H

// nj_result_t: Result codes for njDecode().
typedef enum _nj_result {
    NJ_OK = 0,        // no error, decoding successful
    NJ_NO_JPEG,       // not a JPEG file
    NJ_UNSUPPORTED,   // unsupported format
    NJ_OUT_OF_MEM,    // out of memory
    NJ_INTERNAL_ERR,  // internal error
    NJ_SYNTAX_ERROR,  // syntax error
    __NJ_FINISHED,    // used internally, will never be reported
} nj_result_t;

// njInit: Initialize NanoJPEG.
// For safety reasons, this should be called at least one time before using
// using any of the other NanoJPEG functions.
void njInit(void);

// njDecode: Decode a JPEG image.
// Decodes a memory dump of a JPEG file into internal buffers.
// Parameters:
//   jpeg = The pointer to the memory dump.
//   size = The size of the JPEG file.
// Return value: The error code in case of failure, or NJ_OK (zero) on success.
nj_result_t njDecode(const void* jpeg, const int size);

// njGetWidth: Return the width (in pixels) of the most recently decoded
// image. If njDecode() failed, the result of njGetWidth() is undefined.
int njGetWidth(void);

// njGetHeight: Return the height (in pixels) of the most recently decoded
// image. If njDecode() failed, the result of njGetHeight() is undefined.
int njGetHeight(void);

// njIsColor: Return 1 if the most recently decoded image is a color image
// (RGB) or 0 if it is a grayscale image. If njDecode() failed, the result
// of njGetWidth() is undefined.
int njIsColor(void);

// njGetImage: Returns the decoded image data.
// Returns a pointer to the most recently image. The memory layout it byte-
// oriented, top-down, without any padding between lines. Pixels of color
// images will be stored as three consecutive bytes for the red, green and
// blue channels. This data format is thus compatible with the PGM or PPM
// file formats and the OpenGL texture formats GL_LUMINANCE8 or GL_RGB8.
// If njDecode() failed, the result of njGetImage() is undefined.
unsigned char* njGetImage(void);

// njGetImageSize: Returns the size (in bytes) of the image data returned
// by njGetImage(). If njDecode() failed, the result of njGetImageSize() is
// undefined.
int njGetImageSize(void);

// njGetPrecision: Bits per component, 8 or 16
int njGetPrecision(void);

// njDone: Uninitialize NanoJPEG.
// Resets NanoJPEG's internal state and frees all memory that has been
// allocated at run-time by NanoJPEG. It is still possible to decode another
// image after a njDone() call.
void njDone(void);

#endif//_NANOJPEG_H


///////////////////////////////////////////////////////////////////////////////
// CONFIGURATION SECTION                                                     //
// adjust the default settings for the NJ_ defines here                      //
///////////////////////////////////////////////////////////////////////////////

#ifndef NJ_USE_LIBC
    #define NJ_USE_LIBC 1
#endif

#ifndef NJ_USE_WIN32
  #ifdef _MSC_VER
    #define NJ_USE_WIN32 (!NJ_USE_LIBC)
  #else
    #define NJ_USE_WIN32 0
  #endif
#endif

#ifndef NJ_CHROMA_FILTER
    #define NJ_CHROMA_FILTER 1
#endif


///////////////////////////////////////////////////////////////////////////////
// EXAMPLE PROGRAM                                                           //
// just define _NJ_EXAMPLE_PROGRAM to compile this (requires NJ_USE_LIBC)    //
///////////////////////////////////////////////////////////////////////////////

#ifdef  _NJ_EXAMPLE_PROGRAM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(int argc, char* argv[]) {
    int size;
    unsigned char *buf;
    FILE *f;

    if (argc < 2) { printf("Usage: %s <input.jpg> [<output.pgm>]\n", argv[0]); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { printf("Error opening the input file.\n"); return 1; }
    fseek(f, 0, SEEK_END);
    size = (int) ftell(f);
    buf = (unsigned char*) malloc(size);
    fseek(f, 0, SEEK_SET);
    size = (int) fread(buf, 1, size, f);
    fclose(f);

    njInit();
    if (njDecode(buf, size)) {
        free((void*)buf);
        printf("Error decoding the input file.\n");
        return 1;
    }
    free((void*)buf);

    int precision = njGetPrecision();
    int iscolor = njIsColor();
    const char *outname = (argc > 2) ? argv[2] : (iscolor ? "nanojpeg_out.ppm" : "nanojpeg_out.pgm");
    f = fopen(outname, "wb");
    if (!f) { printf("Error opening the output file.\n"); return 1; }

    if (iscolor) {
        /* baseline color (8-bit) -> P6 */
        fprintf(f, "P6\n%d %d\n255\n", njGetWidth(), njGetHeight());
        fwrite(njGetImage(), 1, njGetImageSize(), f);
    } else {
        /* grayscale. If precision>8 write P5 with 2-byte big-endian samples and appropriate maxval */
        if (precision <= 8) {
            fprintf(f, "P5\n%d %d\n255\n", njGetWidth(), njGetHeight());
            fwrite(njGetImage(), 1, njGetImageSize(), f);
        } else {
            int w = njGetWidth(), h = njGetHeight();
            unsigned short maxv = (unsigned short)((1u << precision) - 1u);
            fprintf(f, "P5\n%d %d\n%d\n", w, h, maxv);
            /* write big-endian 2-byte samples */
            int mn = 65535;
            int mx = 0;
            unsigned short *samples = (unsigned short*) njGetImage();
            for (int i = 0; i < w * h; ++i) {
                unsigned short v = samples[i];
                if (v < mn) mn = v;
                if (v > mx) mx = v;
                unsigned char outb[2];
                outb[0] = (unsigned char)((v >> 8) & 0xFF);
                outb[1] = (unsigned char)(v & 0xFF);
                fwrite(outb, 1, 2, f);
            }
            printf("16-bit pixel range %d..%d\n", mn, mx);
        }
    }
    fclose(f);
    njDone();
    return 0;
}

#endif

///////////////////////////////////////////////////////////////////////////////
// IMPLEMENTATION SECTION                                                    //
// you may stop reading here                                                 //
///////////////////////////////////////////////////////////////////////////////

#ifndef _NJ_INCLUDE_HEADER_ONLY

#ifdef _MSC_VER
    #define NJ_INLINE static __inline
    #define NJ_FORCE_INLINE static __forceinline
#else
    #define NJ_INLINE static inline
    #define NJ_FORCE_INLINE static inline
#endif

#if NJ_USE_LIBC
    #include <stdlib.h>
    #include <string.h>
    #include <math.h>
    #define njAllocMem malloc
    #define njFreeMem  free
    #define njFillMem  memset
    #define njCopyMem  memcpy
#elif NJ_USE_WIN32
    #include <windows.h>
    #define njAllocMem(size) ((void*) LocalAlloc(LMEM_FIXED, (SIZE_T)(size)))
    #define njFreeMem(block) ((void) LocalFree((HLOCAL) block))
    NJ_INLINE void njFillMem(void* block, unsigned char value, int count) { __asm {
        mov edi, block
        mov al, value
        mov ecx, count
        rep stosb
    } }
    NJ_INLINE void njCopyMem(void* dest, const void* src, int count) { __asm {
        mov edi, dest
        mov esi, src
        mov ecx, count
        rep movsb
    } }
#else
    extern void* njAllocMem(int size);
    extern void njFreeMem(void* block);
    extern void njFillMem(void* block, unsigned char byte, int size);
    extern void njCopyMem(void* dest, const void* src, int size);
#endif

typedef struct _nj_code {
    unsigned char bits, code;
} nj_vlc_code_t;

typedef struct _nj_cmp {
    int cid;
    int ssx, ssy;
    int width, height;
    int stride;
    int qtsel;
    int actabsel, dctabsel;
    int dcpred;
    unsigned char *pixels;
} nj_component_t;

typedef struct _nj_ctx {
    nj_result_t error;
    const unsigned char *pos;
    int size;
    int length;
    int width, height;
    int mbwidth, mbheight;
    int mbsizex, mbsizey;
    int ncomp;
    int lossless;
    int precision;
    nj_component_t comp[3];
    int qtused, qtavail;
    unsigned char qtab[4][64];
    nj_vlc_code_t vlctab[4][65536];
    int buf, bufbits;
    int block[64];
    int rstinterval;
    unsigned char *rgb;
} nj_context_t;

static nj_context_t nj;

/* THP video stores an *unescaped* entropy scan: a raw 0xFF byte in the coded
 * data is NOT byte-stuffed as 0xFF 0x00, and the scan carries no restart/EOI
 * markers (wiki.multimedia.cx THP). Kept outside nj_context_t because
 * njDone()/njInit() zero that struct on every njDecode(). */
static int nj_unescaped = 0;
void njSetUnescaped(int v) { nj_unescaped = v ? 1 : 0; }

static const char njZZ[64] = { 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18,
11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28, 35,
42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45,
38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

NJ_FORCE_INLINE unsigned char njClip(const int x) {
    return (x < 0) ? 0 : ((x > 0xFF) ? 0xFF : (unsigned char) x);
}

#define W1 2841
#define W2 2676
#define W3 2408
#define W5 1609
#define W6 1108
#define W7 565

NJ_INLINE void njRowIDCT(int* blk) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = blk[4] << 11)
        | (x2 = blk[6])
        | (x3 = blk[2])
        | (x4 = blk[1])
        | (x5 = blk[7])
        | (x6 = blk[5])
        | (x7 = blk[3])))
    {
        blk[0] = blk[1] = blk[2] = blk[3] = blk[4] = blk[5] = blk[6] = blk[7] = blk[0] << 3;
        return;
    }
    x0 = (blk[0] << 11) + 128;
    x8 = W7 * (x4 + x5);
    x4 = x8 + (W1 - W7) * x4;
    x5 = x8 - (W1 + W7) * x5;
    x8 = W3 * (x6 + x7);
    x6 = x8 - (W3 - W5) * x6;
    x7 = x8 - (W3 + W5) * x7;
    x8 = x0 + x1;
    x0 -= x1;
    x1 = W6 * (x3 + x2);
    x2 = x1 - (W2 + W6) * x2;
    x3 = x1 + (W2 - W6) * x3;
    x1 = x4 + x6;
    x4 -= x6;
    x6 = x5 + x7;
    x5 -= x7;
    x7 = x8 + x3;
    x8 -= x3;
    x3 = x0 + x2;
    x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8;
    x4 = (181 * (x4 - x5) + 128) >> 8;
    blk[0] = (x7 + x1) >> 8;
    blk[1] = (x3 + x2) >> 8;
    blk[2] = (x0 + x4) >> 8;
    blk[3] = (x8 + x6) >> 8;
    blk[4] = (x8 - x6) >> 8;
    blk[5] = (x0 - x4) >> 8;
    blk[6] = (x3 - x2) >> 8;
    blk[7] = (x7 - x1) >> 8;
}

NJ_INLINE void njColIDCT(const int* blk, unsigned char *out, int stride) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = blk[8*4] << 8)
        | (x2 = blk[8*6])
        | (x3 = blk[8*2])
        | (x4 = blk[8*1])
        | (x5 = blk[8*7])
        | (x6 = blk[8*5])
        | (x7 = blk[8*3])))
    {
        x1 = njClip(((blk[0] + 32) >> 6) + 128);
        for (x0 = 8;  x0;  --x0) {
            *out = (unsigned char) x1;
            out += stride;
        }
        return;
    }
    x0 = (blk[0] << 8) + 8192;
    x8 = W7 * (x4 + x5) + 4;
    x4 = (x8 + (W1 - W7) * x4) >> 3;
    x5 = (x8 - (W1 + W7) * x5) >> 3;
    x8 = W3 * (x6 + x7) + 4;
    x6 = (x8 - (W3 - W5) * x6) >> 3;
    x7 = (x8 - (W3 + W5) * x7) >> 3;
    x8 = x0 + x1;
    x0 -= x1;
    x1 = W6 * (x3 + x2) + 4;
    x2 = (x1 - (W2 + W6) * x2) >> 3;
    x3 = (x1 + (W2 - W6) * x3) >> 3;
    x1 = x4 + x6;
    x4 -= x6;
    x6 = x5 + x7;
    x5 -= x7;
    x7 = x8 + x3;
    x8 -= x3;
    x3 = x0 + x2;
    x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8;
    x4 = (181 * (x4 - x5) + 128) >> 8;
    *out = njClip(((x7 + x1) >> 14) + 128);  out += stride;
    *out = njClip(((x3 + x2) >> 14) + 128);  out += stride;
    *out = njClip(((x0 + x4) >> 14) + 128);  out += stride;
    *out = njClip(((x8 + x6) >> 14) + 128);  out += stride;
    *out = njClip(((x8 - x6) >> 14) + 128);  out += stride;
    *out = njClip(((x0 - x4) >> 14) + 128);  out += stride;
    *out = njClip(((x3 - x2) >> 14) + 128);  out += stride;
    *out = njClip(((x7 - x1) >> 14) + 128);
}

#define njThrow(e) do { nj.error = e; return; } while (0)
#define njCheckError() do { if (nj.error) return; } while (0)

static int njShowBits(int bits) {
    unsigned char newbyte;
    if (!bits) return 0;
    while (nj.bufbits < bits) {
        if (nj.size <= 0) {
            nj.buf = (nj.buf << 8) | 0xFF;
            nj.bufbits += 8;
            continue;
        }
        newbyte = *nj.pos++;
        nj.size--;
        nj.bufbits += 8;
        nj.buf = (nj.buf << 8) | newbyte;
        if (newbyte == 0xFF && !nj_unescaped) {
            if (nj.size) {
                unsigned char marker = *nj.pos++;
                nj.size--;
                switch (marker) {
                    case 0x00:
                    case 0xFF:
                        break;
                    case 0xD9: nj.size = 0; break;
                    default:
                        if ((marker & 0xF8) != 0xD0)
                            nj.error = NJ_SYNTAX_ERROR;
                        else {
                            nj.buf = (nj.buf << 8) | marker;
                            nj.bufbits += 8;
                        }
                }
            } else
                nj.error = NJ_SYNTAX_ERROR;
        }
    }
    return (nj.buf >> (nj.bufbits - bits)) & ((1 << bits) - 1);
}

NJ_INLINE void njSkipBits(int bits) {
    if (nj.bufbits < bits)
        (void) njShowBits(bits);
    nj.bufbits -= bits;
}

NJ_INLINE int njGetBits(int bits) {
    int res = njShowBits(bits);
    njSkipBits(bits);
    return res;
}

NJ_INLINE void njByteAlign(void) {
    nj.bufbits &= 0xF8;
}

static void njSkip(int count) {
    nj.pos += count;
    nj.size -= count;
    nj.length -= count;
    if (nj.size < 0) nj.error = NJ_SYNTAX_ERROR;
}

NJ_INLINE unsigned short njDecode16(const unsigned char *pos) {
    return (pos[0] << 8) | pos[1];
}

static void njDecodeLength(void) {
    if (nj.size < 2) njThrow(NJ_SYNTAX_ERROR);
    nj.length = njDecode16(nj.pos);
    if (nj.length > nj.size) njThrow(NJ_SYNTAX_ERROR);
    njSkip(2);
}

NJ_INLINE void njSkipMarker(void) {
    njDecodeLength();
    njSkip(nj.length);
}

NJ_INLINE void njDecodeSOF(void) {
    int i, ssxmax = 0, ssymax = 0;
    nj_component_t* c;
    njDecodeLength(); njCheckError();
    if (nj.length < 9) njThrow(NJ_SYNTAX_ERROR);
    /* support 16-bit precision only for lossless annex; baseline still requires 8 */
    int precision = nj.pos[0];
    if (!nj.lossless && precision != 8) njThrow(NJ_UNSUPPORTED);
    if (precision < 1 || precision > 16) njThrow(NJ_UNSUPPORTED);
    nj.precision = precision;
    nj.height = njDecode16(nj.pos+1);
    nj.width = njDecode16(nj.pos+3);
    if (!nj.width || !nj.height) njThrow(NJ_SYNTAX_ERROR);
    nj.ncomp = nj.pos[5];
    njSkip(6);
    switch (nj.ncomp) { case 1: case 3: break; default: njThrow(NJ_UNSUPPORTED); }
    if (nj.length < (nj.ncomp * 3)) njThrow(NJ_SYNTAX_ERROR);
    for (i = 0, c = nj.comp;  i < nj.ncomp;  ++i, ++c) {
        c->cid = nj.pos[0];
        if (!(c->ssx = nj.pos[1] >> 4)) njThrow(NJ_SYNTAX_ERROR);
        if (c->ssx & (c->ssx - 1)) njThrow(NJ_UNSUPPORTED);
        if (!(c->ssy = nj.pos[1] & 15)) njThrow(NJ_SYNTAX_ERROR);
        if (c->ssy & (c->ssy - 1)) njThrow(NJ_UNSUPPORTED);
        if ((c->qtsel = nj.pos[2]) & 0xFC) njThrow(NJ_SYNTAX_ERROR);
        njSkip(3);
        nj.qtused |= 1 << c->qtsel;
        if (c->ssx > ssxmax) ssxmax = c->ssx;
        if (c->ssy > ssymax) ssymax = c->ssy;
    }

    if (nj.ncomp == 1) { c = nj.comp; c->ssx = c->ssy = ssxmax = ssymax = 1; }

    /* Lossless: subsampling must be 1. If precision>8 we only accept single-component grayscale */
    if (nj.lossless) {
        for (i = 0, c = nj.comp;  i < nj.ncomp;  ++i, ++c) {
            if (c->ssx != 1 || c->ssy != 1) njThrow(NJ_UNSUPPORTED);
            c->width = nj.width;
            c->height = nj.height;
            c->stride = c->width;
            if (nj.precision > 8) {
                if (nj.ncomp != 1) njThrow(NJ_UNSUPPORTED); /* only support single-component for >8 */
                /* allocate width*height unsigned shorts */
                c->pixels = (unsigned char *)njAllocMem(c->width * c->height * sizeof(unsigned short));
                if (!c->pixels) njThrow(NJ_OUT_OF_MEM);
            } else {
                /* allocate width*height bytes */
                c->pixels = (unsigned char *)njAllocMem(c->width * c->height);
                if (!c->pixels) njThrow(NJ_OUT_OF_MEM);
            }
        }
        /* no separate rgb intermediate buffer needed; create it if color and precision==8 */
        if (nj.ncomp == 3 && nj.precision == 8) {
            nj.rgb = (unsigned char*) njAllocMem(nj.width * nj.height * nj.ncomp);
            if (!nj.rgb) njThrow(NJ_OUT_OF_MEM);
        }
        nj.mbsizex = nj.mbsizey = nj.mbwidth = nj.mbheight = 0; /* not used for lossless */
    } else {
        /* baseline path unchanged */
        nj.mbsizex = ssxmax << 3;
        nj.mbsizey = ssymax << 3;
        nj.mbwidth = (nj.width + nj.mbsizex - 1) / nj.mbsizex;
        nj.mbheight = (nj.height + nj.mbsizey - 1) / nj.mbsizey;
        for (i = 0, c = nj.comp;  i < nj.ncomp;  ++i, ++c) {
            c->width = (nj.width * c->ssx + ssxmax - 1) / ssxmax;
            c->height = (nj.height * c->ssy + ssymax - 1) / ssymax;
            c->stride = nj.mbwidth * c->ssx << 3;
            if (((c->width < 3) && (c->ssx != ssxmax)) || ((c->height < 3) && (c->ssy != ssymax))) njThrow(NJ_UNSUPPORTED);
            if (!(c->pixels = (unsigned char *)njAllocMem(c->stride * nj.mbheight * c->ssy << 3))) njThrow(NJ_OUT_OF_MEM);
        }
        if (nj.ncomp == 3) {
            nj.rgb = (unsigned char*) njAllocMem(nj.width * nj.height * nj.ncomp);
            if (!nj.rgb) njThrow(NJ_OUT_OF_MEM);
        }
    }

    njSkip(nj.length);
}

static int njGetVLC(nj_vlc_code_t* vlc, unsigned char* code) {
    int idx = njShowBits(16);
    int bits = vlc[idx].bits;
    if (!bits) { nj.error = NJ_SYNTAX_ERROR; return 0; }
    njSkipBits(bits);
    int value = vlc[idx].code;
    if (code) *code = (unsigned char) value;
    bits = value & 15;
    if (!bits) return 0;
    value = njGetBits(bits);
    /* Defined sign-extension for 'bits' additional bits:
       if the top bit (sign) of the value is zero then the value is negative and
       must be adjusted by subtracting (2^bits - 1). This avoids shifting negatives. */
    if (value < (1 << (bits - 1)))
        value -= (1 << bits) - 1;
    return value;
}

/* 16-bit helpers */

NJ_INLINE void njSetSample16(nj_component_t* c, int y, int x, uint32_t v) {
    /* store clamped 16-bit sample (v is already clamped to 0..0xFFFF) */
    int idx = y * c->stride + x;
    ((uint16_t*) c->pixels)[idx] = (uint16_t) v;
}

//* robust 16-bit clip: input may be negative or huge; returns 0..0xFFFF */
static inline uint32_t njClip16(int64_t val, int bits) {
    if (bits == 16) {
        /* Reference stores 16-bit samples by wrapping (low 16 bits). This preserves
           two's-complement semantics so -1 -> 0xFFFF, etc. */
        return (uint32_t)((uint16_t)val);
    } else {
        /* clamp for other precisions */
        if (val <= 0) return 0u;
        uint64_t maxv = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
        if ((uint64_t)val >= (uint64_t)(maxv + 1ULL)) return (uint32_t)maxv;
        return (uint32_t)val;
    }
}

/* JPEG sign-extend helper (same semantics as reference) */
static inline int jpeg_extend_uint(uint32_t v, int n) {
    uint32_t half = 1u << (n - 1);
    if (v < half) {
        /* produce negative value of n-bit two's complement */
        return (int)(v + (((unsigned int)(~0u) << n) + 1u));
    }
    return (int)v;
}

// Predictor-aware Huffman + extra-bit reader for lossless DC symbols (16-bit case).
static inline int njGetVLC16(nj_vlc_code_t* vlc, int pred)
{
    /* preview 16 bits to index into the expanded vlc table (same as your original) */
    int idx = (int) njShowBits(16);
    if (nj.error) return 0;
    int code_len = vlc[idx].bits;
    if (!code_len) { nj.error = NJ_SYNTAX_ERROR; return 0; }
    njSkipBits(code_len);
    if (nj.error) return 0;
    int symbol = (int)vlc[idx].code;
    if (symbol == 0) return 0;
    /* special-case n == 16: mapping depends on signed predictor */
    if (symbol == 16) {
        /* pred MUST be the signed predictor (int16 semantics). If pred >= 0, return -32768,
           else return +32768. This matches the reference behavior exactly. */
        return (pred >= 0) ? -32768 : 32768;
    }
    /* normal 1..15 case: consume that many bits and sign-extend */
    int nbits = symbol;
    unsigned int raw = (unsigned int) njGetBits(nbits);
    if (nj.error) return 0;
    return jpeg_extend_uint(raw, nbits);
}

// Decode a lossless scan for 16-bit single-component images.
NJ_INLINE void njDecodeScanLossless16(void)
{
    int x, y;
    int predictor;
    nj_component_t *c;
    int rstcount = nj.rstinterval, nextrst = 0;
    int at_restart = 1; // first sample after restart uses default predictor
    int P = nj.precision; // expected to be 16
    if (nj.ncomp != 1) njThrow(NJ_UNSUPPORTED);

    njDecodeLength(); njCheckError();
    if (nj.length < (4 + 2 * nj.ncomp)) njThrow(NJ_SYNTAX_ERROR);

    /* first byte must equal number of components in scan (1) */
    if (nj.pos[0] != nj.ncomp) njThrow(NJ_SYNTAX_ERROR);
    njSkip(1);

    /* read single component selector + Td */
    c = &nj.comp[0];
    if (nj.pos[0] != c->cid) njThrow(NJ_SYNTAX_ERROR);
    if (nj.pos[1] & 0xEE) njThrow(NJ_SYNTAX_ERROR);
    c->dctabsel = nj.pos[1] >> 4;
    njSkip(2);

    /* Ss = predictor (1..7), Se must be 0, Ah must be 0, Al is Pt */
    predictor = nj.pos[0];
    if (predictor < 1 || predictor > 7) njThrow(NJ_UNSUPPORTED);
    if (nj.pos[1] != 0) njThrow(NJ_UNSUPPORTED);
    if ((nj.pos[2] >> 4) != 0) njThrow(NJ_UNSUPPORTED);
    int Pt = nj.pos[2] & 0x0F;
    if (Pt < 0 || Pt > 15) njThrow(NJ_UNSUPPORTED);
    if (Pt > nj.precision) njThrow(NJ_UNSUPPORTED);
    njSkip(3);

    /* default predictor in full-precision signed domain (midpoint) */
    int defaultPredFull = 1 << (P - 1); /* e.g. 32768 for P==16 */

    //printf("precision %d point transform %d prediction %d predictor %d\n", P, Pt, defaultPredFull, predictor);

    /* main decode loop: single component, left-to-right top-to-bottom */
    for (y = 0; y < nj.height; ++y) {
        for (x = 0; x < nj.width; ++x) {

            /* read neighbors as stored uint16_t but interpret as signed int16_t (full precision) */
            int Ra = (x > 0) ? (int16_t)((uint16_t*) c->pixels)[y * c->stride + (x - 1)] : defaultPredFull;
            int Rb = (y > 0) ? (int16_t)((uint16_t*) c->pixels)[(y - 1) * c->stride + x] : defaultPredFull;
            int Rc = (x > 0 && y > 0) ? (int16_t)((uint16_t*) c->pixels)[(y - 1) * c->stride + (x - 1)] : defaultPredFull;

            int Pval; /* predictor in full-precision signed domain */

            /* left column special-cases */
            if (x == 0) {
                if (y == 0 || at_restart) {
                    /* first sample of image or first after restart: use default full-precision predictor */
                    Ra = defaultPredFull;
                    /* compute Pval below using predictor rules */
                } else {
                    /* start of non-first line: Pval is the sample from the line above (signed) */
                    Pval = Rb;

                    /* read diff using predictor-aware VLC (n==16 behavior depends on signed Pval) */
                    int diff = njGetVLC16(&nj.vlctab[c->dctabsel][0], Pval);
                    njCheckError();

                    /* reconstruct in full-precision domain, then apply point-transform (arithmetic) */
                    int recon_full = Pval + diff;
                    if (Pt > 0) recon_full >>= Pt; /* arithmetic shift preserves sign */

                    /* clip to unsigned range and store */
                    unsigned int sample = njClip16((int64_t)recon_full, P);
                    njSetSample16(c, y, x, sample);

                    /* restart handling */
                    if (nj.rstinterval && !(--rstcount)) {
                        njByteAlign();
                        int r = njGetBits(16);
                        if (((r & 0xFFF8) != 0xFFD0) || ((r & 7) != nextrst)) njThrow(NJ_SYNTAX_ERROR);
                        nextrst = (nextrst + 1) & 7;
                        rstcount = nj.rstinterval;
                        at_restart = 1;
                    } else {
                        at_restart = 0;
                    }
                    continue;
                }
            }

            /* compute Pval per predictor from neighbors (full-precision signed) */
            switch (predictor) {
                case 1: Pval = Ra; break;
                case 2: Pval = Rb; break;
                case 3: Pval = Rc; break;
                case 4: Pval = Ra + Rb - Rc; break;
                case 5: Pval = Ra + ((Rb - Rc) >> 1); break;
                case 6: Pval = Rb + ((Ra - Rc) >> 1); break;
                case 7: Pval = (Ra + Rb) >> 1; break;
                default: Pval = 0; break;
            }

            /* decode diff using predictor-aware VLC (n==16 special-case expects signed Pval) */
            int diff = njGetVLC16(&nj.vlctab[c->dctabsel][0], Pval);
            njCheckError();
            /* reconstruct in full-precision signed domain, then apply Pt (arithmetic right-shift) */
            int recon_full = Pval + diff;
            if (Pt > 0) recon_full >>= Pt;

            /* clip to unsigned 0..(2^P - 1) and store as uint16 */
            unsigned int sample = njClip16((int64_t)recon_full, P);
            njSetSample16(c, y, x, sample);
            /* restart handling */
            if (nj.rstinterval && !(--rstcount)) {
                njByteAlign();
                int r = njGetBits(16);
                if (((r & 0xFFF8) != 0xFFD0) || ((r & 7) != nextrst)) njThrow(NJ_SYNTAX_ERROR);
                nextrst = (nextrst + 1) & 7;
                rstcount = nj.rstinterval;
                at_restart = 1;
            } else {
                at_restart = 0;
            }
        }
    }

    nj.error = __NJ_FINISHED;
}

NJ_INLINE void njDecodeScanLossless8(void) {
    int i, x, y;
    int predictor;
    nj_component_t* c;
    int rstcount = nj.rstinterval, nextrst = 0;
    int at_restart = 1; /* first sample of scan uses default pred */
    const int P = nj.precision; /* supported precision (bits) */
    int Pt; /* point transform */

    njDecodeLength();
    njCheckError();
    if (nj.length < (4 + 2 * nj.ncomp)) njThrow(NJ_SYNTAX_ERROR);
    if (nj.pos[0] != nj.ncomp) njThrow(NJ_UNSUPPORTED);
    njSkip(1);
    /* read component selectors and DC table selection (Td) */
    for (i = 0, c = nj.comp;  i < nj.ncomp;  ++i, ++c) {
        if (nj.pos[0] != c->cid) njThrow(NJ_SYNTAX_ERROR);
        if (nj.pos[1] & 0xEE) njThrow(NJ_SYNTAX_ERROR);
        c->dctabsel = nj.pos[1] >> 4;
        njSkip(2);
    }
    /* Ss = predictor (1..7), Se must be 0, Ah/Al: Ah must be 0; Al is Pt (point transform) */
    
    predictor = nj.pos[0];
    if (predictor < 1 || predictor > 7) njThrow(NJ_UNSUPPORTED);
    if (nj.pos[1] != 0) njThrow(NJ_UNSUPPORTED); /* Se != 0 not supported */
    /* Ah = nj.pos[2] >> 4; Al = nj.pos[2] & 0x0F (Pt) */
    if ((nj.pos[2] >> 4) != 0) njThrow(NJ_UNSUPPORTED); /* Ah must be 0 for lossless */
    Pt = nj.pos[2] & 0x0F;

    if (Pt < 0 || Pt > 15) njThrow(NJ_UNSUPPORTED); /* sanity */
    if (Pt > P) njThrow(NJ_UNSUPPORTED); /* can't have Pt > precision we support */
    njSkip(3);

    /* compute reduced-precision default predictor value (samples were right-shifted by Pt at encode time) */
    int reduced_bits = P - Pt - 1;
    int defaultPredReduced = (reduced_bits >= 0) ? (1 << reduced_bits) : 0;
    // printf("precision %d point transform %d default %d X*Y %d*%d components %d\n", P, Pt, defaultPredReduced, nj.height, nj.width, nj.ncomp);
    /* main decode loop: note the component sample buffers contain reduced-precision samples */
    for (y = 0;  y < nj.height; ++y) {
        for (x = 0; x < nj.width; ++x) {
            for (i = 0, c = nj.comp;  i < nj.ncomp;  ++i, ++c) {
                int diff = njGetVLC(&nj.vlctab[c->dctabsel][0], NULL);
                njCheckError();

                /* neighbors in reduced-precision domain */
                int Ra = (x > 0) ? c->pixels[y * c->stride + (x - 1)] : 0;
                int Rb = (y > 0) ? c->pixels[(y - 1) * c->stride + x] : 0;
                int Rc = (x > 0 && y > 0) ? c->pixels[(y - 1) * c->stride + (x - 1)] : 0;

                int Pval;

                if (x == 0) {
                    if (y == 0 || at_restart) {
                        Ra = defaultPredReduced; /* spec: use 2^(P-Pt)-1 when Pt nonzero (or 2^P-1 if Pt==0) */
                        /* fall through to predictor switch below */
                    } else {
                        /* start of non-first line: use sample from line above */
                        Pval = Rb;
                        /* reconstruct in reduced domain, then rescale and clip */
                        int recon_reduced = Pval + diff;
                        int recon_full = recon_reduced << Pt;
                        int sample = njClip(recon_full);
                        c->pixels[y * c->stride + x] = (unsigned char) sample;
                        continue;
                    }
                }

                /* compute predictor using reduced-precision neighbor values */
                switch (predictor) {
                    case 1: Pval = Ra; break;
                    case 2: Pval = Rb; break;
                    case 3: Pval = Rc; break;
                    case 4: Pval = Ra + Rb - Rc; break;
                    case 5: Pval = Ra + ((Rb - Rc) >> 1); break;
                    case 6: Pval = Rb + ((Ra - Rc) >> 1); break;
                    case 7: Pval = (Ra + Rb) >> 1; break;
                    default: Pval = 0; break;
                }

                /* reconstruct in reduced-precision domain, then rescale back and clip */
                int recon_reduced = Pval + diff;
                int recon_full = recon_reduced << Pt; /* multiply by 2^Pt (rescale) */
                int sample = njClip(recon_full);
                c->pixels[y * c->stride + x] = (unsigned char) sample;
            }

            /* restart handling (one MCU = one sample per component in lossless) */
            if (nj.rstinterval && !(--rstcount)) {
                njByteAlign();
                i = njGetBits(16);
                if (((i & 0xFFF8) != 0xFFD0) || ((i & 7) != nextrst)) njThrow(NJ_SYNTAX_ERROR);
                nextrst = (nextrst + 1) & 7;
                rstcount = nj.rstinterval;
                at_restart = 1; /* next decoded sample uses defaultPredReduced */
            } else {
                at_restart = 0;
            }
        }
    }
    nj.error = __NJ_FINISHED;
}

NJ_INLINE void njDecodeDHT(void) {
    int codelen, currcnt, remain, spread, i, j;
    nj_vlc_code_t *vlc;
    static unsigned char counts[16];
    njDecodeLength();
    njCheckError();
    while (nj.length >= 17) {
        i = nj.pos[0];
        if (i & 0xEC) njThrow(NJ_SYNTAX_ERROR);
        if (i & 0x02) njThrow(NJ_UNSUPPORTED);
        i = (i | (i >> 3)) & 3;  // combined DC/AC + tableid value
        for (codelen = 1;  codelen <= 16;  ++codelen)
            counts[codelen - 1] = nj.pos[codelen];
        njSkip(17);
        vlc = &nj.vlctab[i][0];
        remain = spread = 65536;
        for (codelen = 1;  codelen <= 16;  ++codelen) {
            spread >>= 1;
            currcnt = counts[codelen - 1];
            if (!currcnt) continue;
            if (nj.length < currcnt) njThrow(NJ_SYNTAX_ERROR);
            remain -= currcnt << (16 - codelen);
            if (remain < 0) njThrow(NJ_SYNTAX_ERROR);
            for (i = 0;  i < currcnt;  ++i) {
                unsigned char code = nj.pos[i];
                for (j = spread;  j;  --j) {
                    vlc->bits = (unsigned char) codelen;
                    vlc->code = code;
                    ++vlc;
                }
            }
            njSkip(currcnt);
        }
        while (remain--) {
            vlc->bits = 0;
            ++vlc;
        }
    }
    if (nj.length) njThrow(NJ_SYNTAX_ERROR);
}

NJ_INLINE void njDecodeDQT(void) {
    int i;
    unsigned char *t;
    njDecodeLength();
    njCheckError();
    while (nj.length >= 65) {
        i = nj.pos[0];
        if (i & 0xFC) njThrow(NJ_SYNTAX_ERROR);
        nj.qtavail |= 1 << i;
        t = &nj.qtab[i][0];
        for (i = 0;  i < 64;  ++i)
            t[i] = nj.pos[i + 1];
        njSkip(65);
    }
    if (nj.length) njThrow(NJ_SYNTAX_ERROR);
}

NJ_INLINE void njDecodeDRI(void) {
    njDecodeLength();
    njCheckError();
    if (nj.length < 2) njThrow(NJ_SYNTAX_ERROR);
    nj.rstinterval = njDecode16(nj.pos);
    njSkip(nj.length);
}

NJ_INLINE void njDecodeBlock(nj_component_t* c, unsigned char* out) {
    unsigned char code = 0;
    int value, coef = 0;
    njFillMem(nj.block, 0, sizeof(nj.block));
    c->dcpred += njGetVLC(&nj.vlctab[c->dctabsel][0], NULL);
    nj.block[0] = (c->dcpred) * nj.qtab[c->qtsel][0];
    do {
        value = njGetVLC(&nj.vlctab[c->actabsel][0], &code);
        if (!code) break;  // EOB
        if (!(code & 0x0F) && (code != 0xF0)) njThrow(NJ_SYNTAX_ERROR);
        coef += (code >> 4) + 1;
        if (coef > 63) njThrow(NJ_SYNTAX_ERROR);
        nj.block[(int) njZZ[coef]] = value * nj.qtab[c->qtsel][coef];
    } while (coef < 63);
    for (coef = 0;  coef < 64;  coef += 8)
        njRowIDCT(&nj.block[coef]);
    for (coef = 0;  coef < 8;  ++coef)
        njColIDCT(&nj.block[coef], &out[coef], c->stride);
}

NJ_INLINE void njDecodeScan(void) {
    int i, mbx, mby, sbx, sby;
    int rstcount = nj.rstinterval, nextrst = 0;
    nj_component_t* c;
    njDecodeLength();
    njCheckError();
    if (nj.length < (4 + 2 * nj.ncomp)) njThrow(NJ_SYNTAX_ERROR);
    if (nj.pos[0] != nj.ncomp) njThrow(NJ_UNSUPPORTED);
    njSkip(1);
    for (i = 0, c = nj.comp;  i < nj.ncomp;  ++i, ++c) {
        if (nj.pos[0] != c->cid) njThrow(NJ_SYNTAX_ERROR);
        if (nj.pos[1] & 0xEE) njThrow(NJ_SYNTAX_ERROR);
        c->dctabsel = nj.pos[1] >> 4;
        c->actabsel = (nj.pos[1] & 1) | 2;
        njSkip(2);
    }
    if (nj.pos[0] || (nj.pos[1] != 63) || nj.pos[2]) njThrow(NJ_UNSUPPORTED);
    njSkip(nj.length);
    for (mbx = mby = 0;;) {
        for (i = 0, c = nj.comp;  i < nj.ncomp;  ++i, ++c)
            for (sby = 0;  sby < c->ssy;  ++sby)
                for (sbx = 0;  sbx < c->ssx;  ++sbx) {
                    njDecodeBlock(c, &c->pixels[((mby * c->ssy + sby) * c->stride + mbx * c->ssx + sbx) << 3]);
                    njCheckError();
                }
        if (++mbx >= nj.mbwidth) {
            mbx = 0;
            if (++mby >= nj.mbheight) break;
        }
        if (nj.rstinterval && !(--rstcount)) {
            njByteAlign();
            i = njGetBits(16);
            if (((i & 0xFFF8) != 0xFFD0) || ((i & 7) != nextrst)) njThrow(NJ_SYNTAX_ERROR);
            nextrst = (nextrst + 1) & 7;
            rstcount = nj.rstinterval;
            for (i = 0;  i < 3;  ++i)
                nj.comp[i].dcpred = 0;
        }
    }
    nj.error = __NJ_FINISHED;
}

#if NJ_CHROMA_FILTER

#define CF4A (-9)
#define CF4B (111)
#define CF4C (29)
#define CF4D (-3)
#define CF3A (28)
#define CF3B (109)
#define CF3C (-9)
#define CF3X (104)
#define CF3Y (27)
#define CF3Z (-3)
#define CF2A (139)
#define CF2B (-11)
#define CF(x) njClip(((x) + 64) >> 7)

NJ_INLINE void njUpsampleH(nj_component_t* c) {
    const int xmax = c->width - 3;
    unsigned char *out, *lin, *lout;
    int x, y;
    out = (unsigned char*) njAllocMem((c->width * c->height) << 1);
    if (!out) njThrow(NJ_OUT_OF_MEM);
    lin = c->pixels;
    lout = out;
    for (y = c->height;  y;  --y) {
        lout[0] = CF(CF2A * lin[0] + CF2B * lin[1]);
        lout[1] = CF(CF3X * lin[0] + CF3Y * lin[1] + CF3Z * lin[2]);
        lout[2] = CF(CF3A * lin[0] + CF3B * lin[1] + CF3C * lin[2]);
        for (x = 0;  x < xmax;  ++x) {
            lout[(x << 1) + 3] = CF(CF4A * lin[x] + CF4B * lin[x + 1] + CF4C * lin[x + 2] + CF4D * lin[x + 3]);
            lout[(x << 1) + 4] = CF(CF4D * lin[x] + CF4C * lin[x + 1] + CF4B * lin[x + 2] + CF4A * lin[x + 3]);
        }
        lin += c->stride;
        lout += c->width << 1;
        lout[-3] = CF(CF3A * lin[-1] + CF3B * lin[-2] + CF3C * lin[-3]);
        lout[-2] = CF(CF3X * lin[-1] + CF3Y * lin[-2] + CF3Z * lin[-3]);
        lout[-1] = CF(CF2A * lin[-1] + CF2B * lin[-2]);
    }
    c->width <<= 1;
    c->stride = c->width;
    njFreeMem((void*)c->pixels);
    c->pixels = out;
}

NJ_INLINE void njUpsampleV(nj_component_t* c) {
    const int w = c->width, s1 = c->stride, s2 = s1 + s1;
    unsigned char *out, *cin, *cout;
    int x, y;
    out = (unsigned char*) njAllocMem((c->width * c->height) << 1);
    if (!out) njThrow(NJ_OUT_OF_MEM);
    for (x = 0;  x < w;  ++x) {
        cin = &c->pixels[x];
        cout = &out[x];
        *cout = CF(CF2A * cin[0] + CF2B * cin[s1]);  cout += w;
        *cout = CF(CF3X * cin[0] + CF3Y * cin[s1] + CF3Z * cin[s2]);  cout += w;
        *cout = CF(CF3A * cin[0] + CF3B * cin[s1] + CF3C * cin[s2]);  cout += w;
        cin += s1;
        for (y = c->height - 3;  y;  --y) {
            *cout = CF(CF4A * cin[-s1] + CF4B * cin[0] + CF4C * cin[s1] + CF4D * cin[s2]);  cout += w;
            *cout = CF(CF4D * cin[-s1] + CF4C * cin[0] + CF4B * cin[s1] + CF4A * cin[s2]);  cout += w;
            cin += s1;
        }
        cin += s1;
        *cout = CF(CF3A * cin[0] + CF3B * cin[-s1] + CF3C * cin[-s2]);  cout += w;
        *cout = CF(CF3X * cin[0] + CF3Y * cin[-s1] + CF3Z * cin[-s2]);  cout += w;
        *cout = CF(CF2A * cin[0] + CF2B * cin[-s1]);
    }
    c->height <<= 1;
    c->stride = c->width;
    njFreeMem((void*) c->pixels);
    c->pixels = out;
}

#else

NJ_INLINE void njUpsample(nj_component_t* c) {
    int x, y, xshift = 0, yshift = 0;
    unsigned char *out, *lin, *lout;
    while (c->width < nj.width) { c->width <<= 1; ++xshift; }
    while (c->height < nj.height) { c->height <<= 1; ++yshift; }
    out = (unsigned char*) njAllocMem(c->width * c->height);
    if (!out) njThrow(NJ_OUT_OF_MEM);
    lin = c->pixels;
    lout = out;
    for (y = 0;  y < c->height;  ++y) {
        lin = &c->pixels[(y >> yshift) * c->stride];
        for (x = 0;  x < c->width;  ++x)
            lout[x] = lin[x >> xshift];
        lout += c->width;
    }
    c->stride = c->width;
    njFreeMem((void*) c->pixels);
    c->pixels = out;
}

#endif

NJ_INLINE void njConvert(void) {
  /* lossless convert: map components by cid when present */
  if (nj.lossless) {
      int x,y,idx;
      if (nj.ncomp == 1) {
          /* Grayscale lossless: data already in comp[0].pixels, no RGB buffer allocated.
             Nothing to convert here — just return. */
          return;
      }
      /*if (nj.ncomp == 1) {
          unsigned char *src = nj.comp[0].pixels;
          unsigned char *dst = nj.rgb;
          for (idx = 0; idx < nj.width * nj.height; ++idx) {
              unsigned char v = src[idx];
              dst[idx*3 + 0] = v;
              dst[idx*3 + 1] = v;
              dst[idx*3 + 2] = v;
          }
          return;
      }*/
      if (nj.ncomp == 3) {
          /* find which component index corresponds to R, G, B or Y, Cb, Cr */
          int r_idx = 0, g_idx = 1, b_idx = 2; /* defaults to order */
          int found_rgb = 0, found_ycbcr = 0;
          /* ASCII codes: 'R'==82, 'G'==71, 'B'==66 ; 'Y'==89, 'Cb' often 2 or 'Cb' char unlikely */
          for (idx = 0; idx < 3; ++idx) {
              int cid = nj.comp[idx].cid;
              if (cid == 82) r_idx = idx, found_rgb++;
              else if (cid == 71) g_idx = idx, found_rgb++;
              else if (cid == 66) b_idx = idx, found_rgb++;
              else if (cid == 1) { /* common numeric Y component id */
                  /* treat numeric ids 1/2/3 as Y/Cb/Cr often */
                  /* map 1->Y, 2->Cb, 3->Cr */
                  if (nj.comp[idx].cid == 1) { r_idx = -1; found_ycbcr++; } /* just mark found */
              }
          }
          unsigned char *d = nj.rgb;
          unsigned char *s0 = nj.comp[0].pixels;
          unsigned char *s1 = nj.comp[1].pixels;
          unsigned char *s2 = nj.comp[2].pixels;
          int w = nj.width, h = nj.height;
  
          if (found_rgb == 3) {
              /* components are R/G/B; map by cid */
              for (y = 0; y < h; ++y) {
                  int row = y * w;
                  for (x = 0; x < w; ++x) {
                      int pos = row + x;
                      unsigned char R = nj.comp[r_idx].pixels[pos];
                      unsigned char G = nj.comp[g_idx].pixels[pos];
                      unsigned char B = nj.comp[b_idx].pixels[pos];
                      d[pos*3 + 0] = R;
                      d[pos*3 + 1] = G;
                      d[pos*3 + 2] = B;
                  }
              }
              return;
          }
  
          /* Heuristic: if component ids are 1,2,3 treat as YCbCr */
          if ( (nj.comp[0].cid==1 && nj.comp[1].cid==2 && nj.comp[2].cid==3) ) {
              for (y = 0; y < h; ++y) {
                  int row = y * w;
                  for (x = 0; x < w; ++x) {
                      int pos = row + x;
                      int Y  = nj.comp[0].pixels[pos];
                      int Cb = nj.comp[1].pixels[pos] - 128;
                      int Cr = nj.comp[2].pixels[pos] - 128;
                      int R = njClip((int)roundf((float)Y + 1.40200f * (float)Cr));
                      int G = njClip((int)roundf((float)Y - 0.344136f * (float)Cb - 0.714136f * (float)Cr));
                      int B = njClip((int)roundf((float)Y + 1.77200f * (float)Cb));
                      d[pos*3 + 0] = (unsigned char) R;
                      d[pos*3 + 1] = (unsigned char) G;
                      d[pos*3 + 2] = (unsigned char) B;
                  }
              }
              return;
          }
  
          /* Default fallback: assume components in natural order 0->R,1->G,2->B */
          for (y = 0; y < h; ++y) {
              int row = y * w;
              for (x = 0; x < w; ++x) {
                  int pos = row + x;
                  d[pos*3 + 0] = s0[pos];
                  d[pos*3 + 1] = s1[pos];
                  d[pos*3 + 2] = s2[pos];
              }
          }
          return;
      }
      njThrow(NJ_UNSUPPORTED);
  }

    int i;
    nj_component_t* c;
    for (i = 0, c = nj.comp;  i < nj.ncomp;  ++i, ++c) {
        #if NJ_CHROMA_FILTER
            while ((c->width < nj.width) || (c->height < nj.height)) {
                if (c->width < nj.width) njUpsampleH(c);
                njCheckError();
                if (c->height < nj.height) njUpsampleV(c);
                njCheckError();
            }
        #else
            if ((c->width < nj.width) || (c->height < nj.height))
                njUpsample(c);
        #endif
        if ((c->width < nj.width) || (c->height < nj.height)) njThrow(NJ_INTERNAL_ERR);
    }
    if (nj.ncomp == 3) {
        // convert to RGB
        int x, yy;
        unsigned char *prgb = nj.rgb;
        const unsigned char *py  = nj.comp[0].pixels;
        const unsigned char *pcb = nj.comp[1].pixels;
        const unsigned char *pcr = nj.comp[2].pixels;
        for (yy = nj.height;  yy;  --yy) {
            for (x = 0;  x < nj.width;  ++x) {
                int y = py[x] << 8;
                int cb = pcb[x] - 128;
                int cr = pcr[x] - 128;
                *prgb++ = njClip((y            + 359 * cr + 128) >> 8);
                *prgb++ = njClip((y -  88 * cb - 183 * cr + 128) >> 8);
                *prgb++ = njClip((y + 454 * cb            + 128) >> 8);
            }
            py += nj.comp[0].stride;
            pcb += nj.comp[1].stride;
            pcr += nj.comp[2].stride;
        }
    } else if (nj.comp[0].width != nj.comp[0].stride) {
        // grayscale -> only remove stride
        unsigned char *pin = &nj.comp[0].pixels[nj.comp[0].stride];
        unsigned char *pout = &nj.comp[0].pixels[nj.comp[0].width];
        int y;
        for (y = nj.comp[0].height - 1;  y;  --y) {
            njCopyMem(pout, pin, nj.comp[0].width);
            pin += nj.comp[0].stride;
            pout += nj.comp[0].width;
        }
        nj.comp[0].stride = nj.comp[0].width;
    }
}

void njInit(void) {
    njFillMem(&nj, 0, sizeof(nj_context_t));
}

void njDone(void) {
    int i;
    for (i = 0;  i < 3;  ++i)
        if (nj.comp[i].pixels) njFreeMem((void*) nj.comp[i].pixels);
    if (nj.rgb) njFreeMem((void*) nj.rgb);
    njInit();
}

nj_result_t njDecode(const void* jpeg, const int size) {
    njDone();
    nj.pos = (const unsigned char*) jpeg;
    nj.size = size & 0x7FFFFFFF;
    if (nj.size < 2) return NJ_NO_JPEG;
    if ((nj.pos[0] ^ 0xFF) | (nj.pos[1] ^ 0xD8)) return NJ_NO_JPEG;
    njSkip(2);
    while (!nj.error) {
        if ((nj.size < 2) || (nj.pos[0] != 0xFF)) return NJ_SYNTAX_ERROR;
        njSkip(2);
        switch (nj.pos[-1]) {
            case 0xC0: nj.lossless = 0; njDecodeSOF();  break;
            case 0xC3: nj.lossless = 1; njDecodeSOF();  break;
            case 0xC4: njDecodeDHT();  break;
            case 0xDB: njDecodeDQT();  break;
            case 0xDD: njDecodeDRI();  break;
            case 0xDA:
                if (nj.lossless && (nj.precision == 8)) {
                    njDecodeScanLossless8();
                }
                else if (nj.lossless && (nj.ncomp == 1) && (nj.precision == 16)) {
                    njDecodeScanLossless16();
                }
                else if (nj.lossless) {
                    return NJ_UNSUPPORTED;
                }
                else {
                    njDecodeScan();
                }
                break;
            case 0xFE: njSkipMarker(); break;
            default:
                if ((nj.pos[-1] & 0xF0) == 0xE0)
                    njSkipMarker();
                else
                    return NJ_UNSUPPORTED;
        }
    }
    if (nj.error != __NJ_FINISHED) return nj.error;
    nj.error = NJ_OK;
    njConvert();
    return nj.error;
}

int njGetWidth(void)            { return nj.width; }
int njGetHeight(void)           { return nj.height; }
int njIsColor(void)             { return (nj.ncomp != 1); }
unsigned char* njGetImage(void) { return (nj.ncomp == 1) ? nj.comp[0].pixels : nj.rgb; }
int njGetImageSize(void)        { return nj.width * nj.height * nj.ncomp * (nj.precision / 8); }
int njGetPrecision(void) { return nj.precision ? nj.precision : 8; }
#endif // _NJ_INCLUDE_HEADER_ONLY