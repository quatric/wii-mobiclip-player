/*
 * Standalone Mobiclip video decoder (Wii .mo), ported from FFmpeg's
 * libavcodec/mobiclip.c (LGPL) by Florian Nouwt / Adib Surani / Paul B Mahol.
 * Stripped of all FFmpeg infrastructure for bare-metal devkitPPC use.
 */
#ifndef MOBI_DEC_H
#define MOBI_DEC_H

#include <stdint.h>

/* A decoded YUV420P frame. Planes are owned by the decoder. */
typedef struct MoFrame {
    int width, height;
    uint8_t *data[3];      /* Y, U, V */
    int linesize[3];
    int key_frame;
    int moflex;            /* 0 = YCgCo (native Wii), 1 = BT.601 YCbCr */
} MoFrame;

typedef struct MobiDecoder MobiDecoder;

/* width/height must be multiples of 16. Returns NULL on failure. */
MobiDecoder *mobi_open(int width, int height);
void mobi_close(MobiDecoder *d);

/*
 * Decode one compressed video packet. On success returns 0 and sets *out to
 * an internal frame valid until the next call. Returns <0 on error.
 */
int mobi_decode(MobiDecoder *d, const uint8_t *data, int size, MoFrame **out);

#endif
