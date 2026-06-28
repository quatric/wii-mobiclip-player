/*
 * Mobiclip .mo (Wii MOC5) container demuxer — standalone, reads from a FILE*.
 * Ported from FFmpeg libavformat/modec.c (LGPL, (c) 2022 Spotlight Deveaux).
 */
#ifndef MO_DEMUX_H
#define MO_DEMUX_H

#include <stdint.h>
#include <stdio.h>

enum {
    MO_AUDIO_NONE = 0,
    MO_AUDIO_FASTAUDIO,        /* mono   */
    MO_AUDIO_FASTAUDIO_STEREO,
    MO_AUDIO_PCM,              /* s16le stereo */
    MO_AUDIO_ADPCM,            /* IMA mobiclip-wii mono   */
    MO_AUDIO_ADPCM_STEREO,
    MO_AUDIO_VORBIS,           /* Ogg Vorbis (Tremor), channels/rate from header */
};

typedef struct MoDemux {
    FILE *f;

    int    width, height;
    int    fps_num, fps_den;   /* fps = fps_num / fps_den (num=256) */
    int    frame_count;

    int    audio_type;
    int    sample_rate;
    int    channels;

    /* Vorbis: the three setup header packets (id, comment, setup) */
    uint8_t *vh[3];
    int      vh_size[3];

    /* per-packet streaming state */
    long   data_start;
    int    cur_frame;
    int    eof;
} MoDemux;

/* A demuxed packet (points into a reusable internal buffer). */
typedef struct MoPacket {
    int      is_audio;
    uint8_t *data;
    int      size;
    int      frame_index;
} MoPacket;

/* Open and parse header. Returns 0 on success. */
int  mo_demux_open(MoDemux *m, const char *path);
void mo_demux_close(MoDemux *m);

/*
 * Read the next chunk. A chunk yields one video packet then one audio packet.
 * Returns 1 if a packet was produced, 0 at EOF, <0 on error.
 * pkt->data is valid until the next call.
 */
int  mo_demux_read(MoDemux *m, MoPacket *pkt);

#endif
