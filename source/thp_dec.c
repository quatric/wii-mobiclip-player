/*
 * THP (GameCube/Wii video) decoder for the Wii player.
 *
 * Container parse ported from FFmpeg libavformat/thp.c; audio is DSP/THP ADPCM
 * ported from the AV_CODEC_ID_ADPCM_THP path in libavcodec/adpcm.c; video is
 * baseline MJPEG decoded by the bundled NanoJPEG in THP "unescaped scan" mode.
 *
 * The header/index and audio are parsed in one temporary whole-file pass.
 * Afterwards the file image is released and independent JPEG frames are read
 * from storage on demand, keeping runtime memory bounded by one compressed
 * frame instead of the duration/bitrate of the movie.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rgb_source.h"
#include "nanojpeg.h"

static inline uint32_t rb32(const uint8_t *p){
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|(uint32_t)p[3];
}
static inline int32_t sx16(int v){ return (int16_t)v; }
static inline int32_t sx4(int v){ v &= 0xF; return v >= 8 ? v - 16 : v; }
static inline int16_t clip16(int v){ return v < -32768 ? -32768 : (v > 32767 ? 32767 : v); }

typedef struct ThpCtx {
    uint8_t *buf;
    long     size;
    FILE    *f;
    uint8_t *file_buf;
    uint8_t *frame_buf;
    uint32_t frame_cap;

    int      framecnt;
    int      width, height;

    long    *vid_off;
    uint32_t *vid_sz;

    int16_t *audio;        /* interleaved master (owned here) */
} ThpCtx;

/* ---- audio (ADPCM_THP) ---- */

typedef struct { int32_t s1, s2; } ThpChan;

/* Decode one audio packet appended into master (interleaved, oc channels).
 * `ch` = source channels, `oc` = output channels (min(ch,2)). Maintains running
 * per-channel history in st[]. Returns samples decoded (per channel). */
static int thp_decode_audio_packet(const uint8_t *p, int psize, int ch, int oc,
                                    ThpChan *st, int have_status,
                                    int16_t *out /* NULL = count only */)
{
    int table[14][16];
    int nb_samples, hdr, c, need, i;
    const uint8_t *cur;

    if (psize < 8) return 0;
    nb_samples = (int)rb32(p + 4);
    hdr = 8 + 36 * ch;
    if (nb_samples <= 0 || psize < hdr) return 0;

    /* coefficient tables: ch * 16 int16 (big-endian) at offset 8 */
    cur = p + 8;
    for (c = 0; c < ch; c++)
        for (i = 0; i < 16; i++) { table[c][i] = sx16((cur[0]<<8)|cur[1]); cur += 2; }
    /* initial history: ch * (sample1, sample2), used only on the first packet */
    for (c = 0; c < ch; c++) {
        int s1 = sx16((cur[0]<<8)|cur[1]);
        int s2 = sx16((cur[2]<<8)|cur[3]);
        cur += 4;
        if (!have_status) { st[c].s1 = s1; st[c].s2 = s2; }
    }

    if (!out) return nb_samples;

    /* ADPCM data follows, channel by channel */
    need = (nb_samples + 13) / 14;
    for (c = 0; c < ch; c++) {
        int oi = (c < oc) ? c : -1;   /* which output lane, if any */
        int written = 0;
        for (i = 0; i < need; i++) {
            int byte, index, expo, n;
            int64_t f1, f2;
            if (cur >= p + psize) break;
            byte  = *cur++;
            index = (byte >> 4) & 7;
            expo  = byte & 0x0F;
            f1 = table[c][index*2];
            f2 = table[c][index*2 + 1];
            for (n = 0; n < 14 && (i*14 + n) < nb_samples; n++) {
                int32_t sd;
                if (n & 1) {
                    sd = sx4(byte);
                } else {
                    if (cur >= p + psize) { byte = 0; } else byte = *cur++;
                    sd = sx4(byte >> 4);
                }
                sd = (int32_t)(((st[c].s1 * f1 + st[c].s2 * f2) >> 11) + (sd * (1 << expo)));
                sd = clip16(sd);
                st[c].s2 = st[c].s1;
                st[c].s1 = sd;
                if (oi >= 0) out[written * oc + oi] = (int16_t)sd;
                written++;
            }
        }
        /* if src has more channels than out, the extra channels only advance
         * the cursor (decoded above), which is what we want */
    }
    return nb_samples;
}

static void thp_build_audio(ThpCtx *c, RgbSource *out,
                            long *audio_off, uint32_t *audio_sz,
                            int ach, int arate)
{
    int oc = ach >= 2 ? 2 : 1;
    long total = 0;
    int f;
    ThpChan st[14];
    int16_t *master;
    int have_status = 0;
    long wpos = 0;

    memset(st, 0, sizeof(st));

    for (f = 0; f < c->framecnt; f++) {
        if (!audio_sz[f]) continue;
        total += thp_decode_audio_packet(c->buf + audio_off[f], audio_sz[f],
                                         ach, oc, st, 1, NULL);
    }
    if (total <= 0) return;

    master = calloc((size_t)total * oc, sizeof(int16_t));
    if (!master) return;

    memset(st, 0, sizeof(st));
    have_status = 0;
    for (f = 0; f < c->framecnt; f++) {
        int got;
        if (!audio_sz[f]) continue;
        got = thp_decode_audio_packet(c->buf + audio_off[f], audio_sz[f],
                                      ach, oc, st, have_status,
                                      master + wpos * oc);
        have_status = 1;
        wpos += got;
        if (wpos > total) break;
    }

    c->audio = master;
    out->audio = master;
    out->audio_samples = total;
    out->sample_rate = arate > 0 ? arate : 32000;
    out->channels = oc;
}

/* ---- video ---- */

static int thp_get_frame(RgbSource *s, int idx, uint8_t *dst)
{
    ThpCtx *c = s->priv;
    uint32_t size;
    int n = c->width * c->height;

    if (idx < 0 || idx >= c->framecnt || !c->f) return -1;
    size = c->vid_sz[idx];
    if (size > c->frame_cap) {
        uint8_t *next = realloc(c->frame_buf, size);
        if (!next) return -1;
        c->frame_buf = next;
        c->frame_cap = size;
    }
    if (fseek(c->f, c->vid_off[idx], SEEK_SET) != 0 ||
        fread(c->frame_buf, 1, size, c->f) != size)
        return -1;

    njDone();   /* free the previous frame's buffers, then re-init (njDone ends
                 * with njInit); njInit alone would leak them every frame -> OOM */
    njSetUnescaped(1);
    if (njDecode(c->frame_buf, (int)size) != NJ_OK) return -1;
    if (njGetWidth() != c->width || njGetHeight() != c->height) return -1;

    if (njIsColor()) {
        memcpy(dst, njGetImage(), (size_t)n * 3);
    } else {
        const uint8_t *g = njGetImage();
        for (int i = 0; i < n; i++) { dst[i*3]=dst[i*3+1]=dst[i*3+2]=g[i]; }
    }
    return 0;
}

static void thp_close(RgbSource *s)
{
    ThpCtx *c = s->priv;
    if (!c) return;
    njDone();
    if (c->f) fclose(c->f);
    free(c->file_buf); free(c->frame_buf);
    free(c->buf); free(c->vid_off); free(c->vid_sz);
    free(c->audio);
    free(c);
    s->priv = NULL;
}

int thp_open(RgbSource *out, const char *path)
{
    ThpCtx *c;
    FILE *f;
    long sz, off;
    uint32_t framecnt, first_framesz, compoff, first_frame, framesz;
    uint32_t fps_bits;
    float fps;
    int compcount, i, has_audio = 0, ach = 0, arate = 0;
    long *audio_off = NULL;
    uint32_t *audio_sz = NULL;
    const uint8_t *cp;

    memset(out, 0, sizeof(*out));
    c = calloc(1, sizeof(*c));
    if (!c) return -1;

    f = fopen(path, "rb");
    if (!f) { free(c); return -1; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0x30) { fclose(f); free(c); return -1; }
    c->size = sz;
    c->buf = malloc(sz);
    if (!c->buf) { fclose(f); free(c); return -1; }
    if (fread(c->buf, 1, sz, f) != (size_t)sz) { fclose(f); goto fail; }
    fclose(f);

    if (memcmp(c->buf, "THP\0", 4) != 0) goto fail;
    fps_bits      = rb32(c->buf + 16);
    memcpy(&fps, &fps_bits, 4);
    framecnt      = rb32(c->buf + 20);
    first_framesz = rb32(c->buf + 24);
    compoff       = rb32(c->buf + 32);
    first_frame   = rb32(c->buf + 40);

    if (fps < 0.1f || fps > 1000.f || framecnt == 0 || framecnt > 1000000) goto fail;
    if (compoff + 4 + 16 > (uint32_t)sz) goto fail;

    /* component structure */
    compcount = (int)rb32(c->buf + compoff);
    if (compcount <= 0 || compcount > 16) goto fail;
    cp = c->buf + compoff + 4 + 16;   /* per-component fields follow 16 type bytes */
    for (i = 0; i < compcount; i++) {
        int type = c->buf[compoff + 4 + i];
        if (type == 0) {              /* video */
            if (cp + 8 > c->buf + sz) goto fail;
            c->width  = (int)rb32(cp); cp += 4;
            c->height = (int)rb32(cp); cp += 4;
            /* version 0x11000 has one extra u32 here; detect via header[4] */
            if (rb32(c->buf + 4) == 0x11000) cp += 4;
        } else if (type == 1) {       /* audio */
            if (cp + 12 > c->buf + sz) goto fail;
            ach   = (int)rb32(cp); cp += 4;
            arate = (int)rb32(cp); cp += 4;
            cp += 4;                  /* duration */
            has_audio = 1;
        }
    }
    if (c->width <= 0 || c->height <= 0 || c->width > 2048 || c->height > 2048) goto fail;
    if (ach < 0 || ach > 14) goto fail;

    c->framecnt = (int)framecnt;
    c->vid_off = malloc((size_t)framecnt * sizeof(long));
    c->vid_sz  = malloc((size_t)framecnt * sizeof(uint32_t));
    if (has_audio) {
        audio_off = malloc((size_t)framecnt * sizeof(long));
        audio_sz  = malloc((size_t)framecnt * sizeof(uint32_t));
    }
    if (!c->vid_off || !c->vid_sz ||
        (has_audio && (!audio_off || !audio_sz))) goto fail;

    /* walk frame chain */
    off = first_frame;
    framesz = first_framesz;
    for (i = 0; i < (int)framecnt; i++) {
        long h = off;
        uint32_t vsize, asize = 0;
        int nfields = has_audio ? 4 : 3;
        if (h + nfields * 4 > sz) goto fail;
        /* header: [0]=next framesz, [1]=prev size, [2]=video size, [3]=audio size */
        vsize = rb32(c->buf + h + 8);
        if (has_audio) asize = rb32(c->buf + h + 12);
        c->vid_off[i] = h + nfields * 4;
        c->vid_sz[i]  = vsize;
        if (c->vid_off[i] + vsize > sz) goto fail;
        if (has_audio) {
            audio_off[i] = c->vid_off[i] + vsize;
            audio_sz[i]  = asize;
            if (audio_off[i] + asize > sz) goto fail;
        }
        off += (framesz ? framesz : 1);
        framesz = rb32(c->buf + h);   /* size of the next frame */
    }

    out->w = c->width; out->h = c->height;
    out->frame_count = c->framecnt;
    out->fps = fps;
    out->independent_frames = 1;
    out->channels = 1;
    out->get_frame = thp_get_frame;
    out->close = thp_close;
    out->priv = c;

    if (has_audio)
        thp_build_audio(c, out, audio_off, audio_sz, ach, arate);

    /* Audio is now independent of the container image.  Reopen the THP for
     * bounded random frame reads, then release the duration-sized buffer. */
    c->f = fopen(path, "rb");
    if (!c->f) goto fail;
    c->file_buf = malloc(128 * 1024);
    if (c->file_buf) setvbuf(c->f, (char *)c->file_buf, _IOFBF, 128 * 1024);
    free(c->buf);
    c->buf = NULL;

    free(audio_off); free(audio_sz);
    return 0;

fail:
    free(audio_off); free(audio_sz);
    if (c->f) fclose(c->f);
    free(c->file_buf); free(c->frame_buf);
    free(c->buf); free(c->vid_off); free(c->vid_sz);
    free(c->audio);
    free(c);
    return -1;
}
