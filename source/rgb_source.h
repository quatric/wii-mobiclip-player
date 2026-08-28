/*
 * Generic packed-RGB24 video source used by the non-Mobiclip players
 * (KWZ / PPM Flipnotes, THP). Each format module fills one of these and the
 * shared play_rgb() loop in main.c drives it: frames are requested strictly in
 * order (Flipnote inter-frame diffing needs sequential decode), audio is a
 * single pre-decoded interleaved int16 master streamed against the video clock.
 */
#ifndef RGB_SOURCE_H
#define RGB_SOURCE_H

#include <stdint.h>

typedef struct RgbSource {
    int    w, h;              /* frame size in pixels */
    int    frame_count;
    double fps;               /* frames per second (may be fractional) */
    int    loops;             /* 1 => restart at the end (PPM/KWZ Flipnotes) */
    int    independent_frames;/* decoder may jump directly to any frame */

    int16_t *audio;           /* interleaved master PCM, NULL if silent */
    long     audio_samples;   /* per-channel sample count */
    int      sample_rate;
    int      channels;        /* 1 or 2 */

    /* Decode frame `idx` (called in ascending order) into `rgb` (w*h*3 bytes).
     * Returns 0 on success, <0 on error. */
    int  (*get_frame)(struct RgbSource *s, int idx, uint8_t *rgb);
    void (*close)(struct RgbSource *s);

    void *priv;               /* module-private state */
} RgbSource;

/* Openers: return 0 on success and fill *out. */
int kwz_open(RgbSource *out, const char *path);
int ppm_open(RgbSource *out, const char *path);
int thp_open(RgbSource *out, const char *path);

#endif
