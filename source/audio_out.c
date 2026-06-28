#include <asndlib.h>
#include <ogc/cache.h>
#include <string.h>
#include <malloc.h>
#include "audio_out.h"

/* Stereo int16 ring buffer (single producer / single consumer). */
#define RING_FRAMES (1 << 16)            /* stereo sample frames */
static int16_t *ring;                    /* RING_FRAMES * 2 int16 */
static volatile int rd, wr;              /* frame indices */
static int srate;

/* two output blocks ping-ponged by the voice callback */
#define BLK 2048                         /* frames per block */
static int16_t *blk[2];
static int blk_cur;

/* Short fade-in to suppress the start-of-stream click some codecs produce
 * (e.g. FastAudio's IIR filter ramping from a zero state). */
#define FADE_FRAMES 1024
static int faded;

static int ring_count(void)
{
    int c = wr - rd;
    if (c < 0) c += RING_FRAMES;
    return c;
}

static void fill_block(int16_t *b)
{
    for (int i = 0; i < BLK; i++) {
        if (rd != wr) {
            b[i * 2]     = ring[rd * 2];
            b[i * 2 + 1] = ring[rd * 2 + 1];
            rd = (rd + 1) % RING_FRAMES;
        } else {
            b[i * 2] = b[i * 2 + 1] = 0;   /* underrun -> silence */
        }
    }
    DCFlushRange(b, BLK * 2 * sizeof(int16_t));
}

static void voice_cb(s32 voice)
{
    if (ASND_StatusVoice(voice) != SND_WORKING) return;
    int16_t *b = blk[blk_cur];
    fill_block(b);
    DCFlushRange(b, BLK * 2 * sizeof(int16_t));
    ASND_AddVoice(voice, b, BLK * 2 * sizeof(int16_t));
    blk_cur ^= 1;
}

void audio_out_start(int sample_rate)
{
    srate = sample_rate > 0 ? sample_rate : 32000;
    if (!ring) ring = memalign(32, RING_FRAMES * 2 * sizeof(int16_t));
    if (!blk[0]) { blk[0] = memalign(32, BLK * 2 * sizeof(int16_t));
                   blk[1] = memalign(32, BLK * 2 * sizeof(int16_t)); }
    rd = wr = 0;
    blk_cur = 0;
    faded = 0;

    ASND_Init();
    ASND_Pause(0);

    /* prime both blocks with silence, start the voice */
    memset(blk[0], 0, BLK * 2 * sizeof(int16_t));
    DCFlushRange(blk[0], BLK * 2 * sizeof(int16_t));
    ASND_SetVoice(0, VOICE_STEREO_16BIT, srate, 0,
                  blk[0], BLK * 2 * sizeof(int16_t), 255, 255, voice_cb);
                  
    memset(blk[1], 0, BLK * 2 * sizeof(int16_t));
    DCFlushRange(blk[1], BLK * 2 * sizeof(int16_t));
    ASND_AddVoice(0, blk[1], BLK * 2 * sizeof(int16_t));
    
    blk_cur = 0;
}

void audio_out_push(const int16_t *stereo, int nsamples)
{
    for (int i = 0; i < nsamples; i++) {
        int nx = (wr + 1) % RING_FRAMES;
        if (nx == rd) break;             /* full: drop */
        int l = stereo[i * 2], r = stereo[i * 2 + 1];
        if (faded < FADE_FRAMES) {       /* linear ramp on the first frames */
            l = l * faded / FADE_FRAMES;
            r = r * faded / FADE_FRAMES;
            faded++;
        }
        ring[wr * 2]     = l;
        ring[wr * 2 + 1] = r;
        wr = nx;
    }
}

int audio_out_queued(void) { return ring_count(); }

void audio_out_stop(void)
{
    ASND_StopVoice(0);
}

int audio_out_space(void)
{
    return (RING_FRAMES + rd - wr - 1) % RING_FRAMES;
}
