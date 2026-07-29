/*
 * Flipnote Studio (PPM) decoder — standalone port of FFmpeg
 * libavformat/ppmflipdec.c (LGPL) for the Wii player. Whole file in memory,
 * frames decoded to RGB24 on demand (2 one-bit layers, inter-frame XOR diff),
 * all sound tracks pre-decoded/mixed to one mono int16 master at 32768 Hz.
 * Ported from flipnote.js.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rgb_source.h"

#define PPM_W 256
#define PPM_H 192
#define PPM_PIXELS (PPM_W * PPM_H)

#define PPM_RAW_SAMPLE_RATE 8192
#define PPM_SAMPLE_RATE     32768

static inline uint16_t rl16(const uint8_t *p){ return p[0] | (p[1] << 8); }
static inline uint32_t rl32(const uint8_t *p){
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static inline uint32_t rb32(const uint8_t *p){
    return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) | ((uint32_t)p[2]<<8) | (uint32_t)p[3];
}
static inline int clipi(int v, int lo, int hi){ return v < lo ? lo : (v > hi ? hi : v); }
static inline int16_t clip16(int v){ return v < -32768 ? -32768 : (v > 32767 ? 32767 : v); }
#define PMAX(a,b) ((a)>(b)?(a):(b))
#define PMIN(a,b) ((a)<(b)?(a):(b))

static const float PPM_FRAMERATES[9] = { 0.5f, 0.5f, 1, 2, 4, 6, 12, 20, 30 };

/* palette: WHITE, BLACK, RED, BLUE */
static const uint8_t PPM_RGB[4][3] = {
    { 0xff, 0xff, 0xff }, { 0x0e, 0x0e, 0x0e },
    { 0xff, 0x2a, 0x2a }, { 0x0a, 0x39, 0xff },
};

static const int8_t ADPCM_INDEX_TABLE_4BIT[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8
};
static const int16_t ADPCM_STEP_TABLE[] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767, 0
};

typedef struct PpmCtx {
    uint8_t *buf;
    long     size;

    int      frame_count;
    int      frame;             /* sequential decode cursor */

    uint32_t frame_data_length;
    uint32_t *frame_offsets;
    int      num_offsets;

    uint8_t *layer[2];
    uint8_t *prev[2];
    uint8_t  line_enc[2][PPM_H];

    int16_t *audio;
    int      audio_len;
} PpmCtx;

static int16_t *ppm_decode_track(const uint8_t *p, int len, int *out_len)
{
    int16_t *dst;
    int src_size = len, i, dst_ptr = 0;
    int predictor = 0, step_index = 0, low_nibble = 1;
    *out_len = 0;
    if (len <= 0) return NULL;
    dst = malloc((size_t)src_size * 2 * sizeof(*dst));
    if (!dst) return NULL;
    for (i = 0; i < src_size; ) {
        int sample, step, diff;
        if (low_nibble) sample = p[i] & 0xF;
        else            sample = p[i++] >> 4;
        low_nibble = !low_nibble;
        step = ADPCM_STEP_TABLE[step_index];
        diff = step >> 3;
        if (sample & 1) diff += step >> 2;
        if (sample & 2) diff += step >> 1;
        if (sample & 4) diff += step;
        if (sample & 8) diff = -diff;
        predictor  = clip16(predictor + diff);
        step_index = clipi(step_index + ADPCM_INDEX_TABLE_4BIT[sample], 0, 88);
        dst[dst_ptr++] = predictor;
    }
    *out_len = dst_ptr;
    return dst;
}

static int16_t *pcm_resample_nn(const int16_t *src, int src_len,
                                double src_freq, double dst_freq, int *out_len)
{
    int dst_len = (int)((double)src_len / src_freq * dst_freq);
    double adj = src_freq / dst_freq;
    int16_t *dst;
    int i;
    *out_len = 0;
    if (dst_len <= 0) return NULL;
    dst = malloc((size_t)dst_len * sizeof(*dst));
    if (!dst) return NULL;
    for (i = 0; i < dst_len; i++) {
        int sp = (int)(i * adj);
        dst[i] = (sp >= 0 && sp < src_len) ? src[sp] : 0;
    }
    *out_len = dst_len;
    return dst;
}

static void pcm_mix_half(int16_t *dst, int dst_len, const int16_t *src, int src_len, int off)
{
    for (int n = 0; n < src_len; n++) {
        int idx = off + n;
        if (idx >= dst_len) break;
        dst[idx] = clip16(dst[idx] + src[n] / 2);
    }
}

static void ppm_build_audio(PpmCtx *c, int frame_speed, int bgm_speed)
{
    const int dst_freq = PPM_SAMPLE_RATE;
    double framerate = PPM_FRAMERATES[frame_speed];
    double bgmrate   = PPM_FRAMERATES[bgm_speed];
    double duration  = c->frame_count / framerate;
    int master_len   = (int)ceil(duration * dst_freq);
    long sound_off, track_ptr[4];
    uint32_t track_len[4];
    int16_t *master;
    int t, i;

    if (master_len <= 0) return;
    sound_off = 0x6A0 + (long)c->frame_data_length + c->frame_count;
    if (sound_off % 4) sound_off += 4 - (sound_off % 4);
    if (sound_off + 32 > c->size) return;
    for (t = 0; t < 4; t++)
        track_len[t] = rl32(c->buf + sound_off + t * 4);
    track_ptr[0] = sound_off + 32;
    for (t = 1; t < 4; t++)
        track_ptr[t] = track_ptr[t-1] + track_len[t-1];

    for (i = 0, t = 0; t < 4; t++) i += track_len[t];
    if (i == 0) return;

    master = calloc(master_len, sizeof(*master));
    if (!master) return;

    if (track_len[0] > 0 && track_ptr[0] + track_len[0] <= c->size) {
        int raw_len, res_len;
        int16_t *raw = ppm_decode_track(c->buf + track_ptr[0], track_len[0], &raw_len);
        if (raw) {
            double src_freq = PPM_RAW_SAMPLE_RATE * (framerate / bgmrate);
            int16_t *bgm = pcm_resample_nn(raw, raw_len, src_freq, dst_freq, &res_len);
            free(raw);
            if (bgm) { pcm_mix_half(master, master_len, bgm, res_len, 0); free(bgm); }
        }
    }

    {
        int16_t *se[3] = { NULL, NULL, NULL };
        int se_len[3] = { 0, 0, 0 };
        double spf = (double)dst_freq / framerate;
        for (t = 1; t < 4; t++) {
            if (track_len[t] > 0 && track_ptr[t] + track_len[t] <= c->size) {
                int raw_len, res_len;
                int16_t *raw = ppm_decode_track(c->buf + track_ptr[t], track_len[t], &raw_len);
                if (raw) {
                    se[t-1] = pcm_resample_nn(raw, raw_len, PPM_RAW_SAMPLE_RATE,
                                              dst_freq, &res_len);
                    se_len[t-1] = res_len;
                    free(raw);
                }
            }
        }
        if (se[0] || se[1] || se[2]) {
            long flags_ptr = 0x6A0 + (long)c->frame_data_length;
            for (i = 0; i < c->frame_count; i++) {
                int off = (int)ceil(i * spf);
                uint8_t flag;
                if (flags_ptr + i >= c->size) break;
                flag = c->buf[flags_ptr + i];
                if (se[0] && (flag & 0x1)) pcm_mix_half(master, master_len, se[0], se_len[0], off);
                if (se[1] && (flag & 0x2)) pcm_mix_half(master, master_len, se[1], se_len[1], off);
                if (se[2] && (flag & 0x4)) pcm_mix_half(master, master_len, se[2], se_len[2], off);
            }
        }
        for (t = 0; t < 3; t++) free(se[t]);
    }

    c->audio = master;
    c->audio_len = master_len;
}

static void frame_palette(PpmCtx *c, int frame, int pal[3])
{
    uint8_t header = c->buf[c->frame_offsets[frame]];
    int is_inverted = (header & 0x1) != 1;
    int pen_map[4];
    pen_map[0] = is_inverted ? 0 : 1;
    pen_map[1] = is_inverted ? 0 : 1;
    pen_map[2] = 2;
    pen_map[3] = 3;
    pal[0] = is_inverted ? 1 : 0;
    pal[1] = pen_map[(header >> 1) & 0x3];
    pal[2] = pen_map[(header >> 3) & 0x3];
}

/* Decode the layers of frame `f` into c->layer[], applying inter-frame diff.
 * Advances c->prev[]. Assumes f == c->frame (sequential). */
static void ppm_decode_frame(PpmCtx *c, int f)
{
    const uint8_t *p, *end;
    uint8_t header;
    int is_key, is_translated, translate_x = 0, translate_y = 0, layer, y, i;

    p   = c->buf + c->frame_offsets[f];
    end = c->buf + c->size;

    header        = *p++;
    is_key        = (header >> 7) & 0x1;
    is_translated = (header >> 5) & 0x3;

    memset(c->layer[0], 0, PPM_PIXELS);
    memset(c->layer[1], 0, PPM_PIXELS);

    if (is_translated && p + 2 <= end) {
        translate_x = (int8_t)*p++;
        translate_y = (int8_t)*p++;
    }

    for (layer = 0; layer < 2; layer++) {
        int ptr = 0;
        memset(c->line_enc[layer], 0, PPM_H);
        while (ptr < PPM_H) {
            uint8_t byte = (p < end) ? *p++ : 0;
            if (byte == 0) { ptr += 4; continue; }
            c->line_enc[layer][ptr++] =  byte       & 0x03;
            if (ptr < PPM_H) c->line_enc[layer][ptr++] = (byte >> 2) & 0x03;
            if (ptr < PPM_H) c->line_enc[layer][ptr++] = (byte >> 4) & 0x03;
            if (ptr < PPM_H) c->line_enc[layer][ptr++] = (byte >> 6) & 0x03;
        }
    }

    for (layer = 0; layer < 2; layer++) {
        uint8_t *pixel_buffer = c->layer[layer];
        for (y = 0; y < PPM_H; y++) {
            int base = y * PPM_W;
            int line_type = c->line_enc[layer][y];
            switch (line_type) {
            case 0: break;
            case 1:
            case 2: {
                uint32_t line_header;
                int ptr = base;
                if (line_type == 2)
                    memset(pixel_buffer + base, 1, PPM_W);
                if (p + 4 > end) break;
                line_header = rb32(p); p += 4;
                for (; line_header != 0; line_header <<= 1, ptr += 8) {
                    if (line_header & 0x80000000u) {
                        uint8_t chunk = (p < end) ? *p++ : 0;
                        if (line_type == 1) {
                            int pixel;
                            for (pixel = 0; chunk != 0; pixel++, chunk >>= 1)
                                pixel_buffer[ptr + pixel] = chunk & 0x1;
                        } else {
                            int pixel;
                            for (pixel = 0; pixel < 8; pixel++, chunk >>= 1)
                                pixel_buffer[ptr + pixel] = chunk & 0x1;
                        }
                    }
                }
                break;
            }
            case 3: {
                int ptr = base, i2;
                uint8_t chunk = 0;
                for (i2 = 0; i2 < PPM_W; i2++) {
                    if (i2 % 8 == 0)
                        chunk = (p < end) ? *p++ : 0;
                    pixel_buffer[ptr++] = chunk & 0x1;
                    chunk >>= 1;
                }
                break;
            }
            }
        }
    }

    if (!is_key && translate_x == 0 && translate_y == 0) {
        for (i = 0; i < PPM_PIXELS; i++) {
            c->layer[0][i] ^= c->prev[0][i];
            c->layer[1][i] ^= c->prev[1][i];
        }
    } else if (!is_key) {
        int start_x = PMAX(translate_x, 0);
        int start_y = PMAX(translate_y, 0);
        int end_x   = PMIN(PPM_W + translate_x, PPM_W);
        int end_y   = PMIN(PPM_H + translate_y, PPM_H);
        int shift   = translate_y * PPM_W + translate_x;
        int xx, yy;
        for (yy = start_y; yy < end_y; yy++) {
            for (xx = start_x; xx < end_x; xx++) {
                int d = yy * PPM_W + xx;
                int sidx = d - shift;
                c->layer[0][d] ^= c->prev[0][sidx];
                c->layer[1][d] ^= c->prev[1][sidx];
            }
        }
    }
    memcpy(c->prev[0], c->layer[0], PPM_PIXELS);
    memcpy(c->prev[1], c->layer[1], PPM_PIXELS);
}

static int ppm_get_frame(RgbSource *s, int idx, uint8_t *dst)
{
    PpmCtx *c = s->priv;
    int pal[3], i;

    if (idx < 0 || idx >= c->frame_count) return -1;
    while (c->frame < idx) { ppm_decode_frame(c, c->frame); c->frame++; }
    ppm_decode_frame(c, idx);
    c->frame = idx + 1;

    frame_palette(c, idx, pal);
    {
        const uint8_t *paper = PPM_RGB[pal[0]];
        for (i = 0; i < PPM_PIXELS; i++) {
            dst[i*3+0] = paper[0]; dst[i*3+1] = paper[1]; dst[i*3+2] = paper[2];
        }
    }
    {
        static const int order[2] = { 1, 0 };
        for (int o = 0; o < 2; o++) {
            int l = order[o];
            const uint8_t *buf = c->layer[l];
            const uint8_t *col = PPM_RGB[pal[l + 1]];
            for (i = 0; i < PPM_PIXELS; i++) {
                if (!buf[i]) continue;
                dst[i*3+0] = col[0]; dst[i*3+1] = col[1]; dst[i*3+2] = col[2];
            }
        }
    }
    return 0;
}

static void ppm_close(RgbSource *s)
{
    PpmCtx *c = s->priv;
    if (!c) return;
    free(c->buf); free(c->frame_offsets);
    free(c->layer[0]); free(c->layer[1]);
    free(c->prev[0]); free(c->prev[1]);
    free(c->audio);
    free(c);
    s->priv = NULL;
}

int ppm_open(RgbSource *out, const char *path)
{
    PpmCtx *c;
    FILE *f;
    long sz, sound_off;
    uint16_t offset_table_length;
    int n, frame_speed, bgm_speed;

    memset(out, 0, sizeof(*out));
    c = calloc(1, sizeof(*c));
    if (!c) return -1;

    f = fopen(path, "rb");
    if (!f) { free(c); return -1; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0x6A8) { fclose(f); free(c); return -1; }
    c->size = sz;
    c->buf = malloc(sz);
    if (!c->buf) { fclose(f); free(c); return -1; }
    if (fread(c->buf, 1, sz, f) != (size_t)sz) { fclose(f); goto fail; }
    fclose(f);

    c->frame_data_length = rl32(c->buf + 4);
    c->frame_count = rl16(c->buf + 0xC) + 1;

    sound_off = 0x6A0 + (long)c->frame_data_length + c->frame_count;
    if (sound_off % 4) sound_off += 4 - (sound_off % 4);
    if (sound_off + 17 > c->size) goto fail;
    frame_speed = 8 - c->buf[sound_off + 16];
    bgm_speed   = 8 - c->buf[sound_off + 17];
    if (frame_speed < 0 || frame_speed > 8) goto fail;
    if (bgm_speed < 0 || bgm_speed > 8) bgm_speed = frame_speed;

    offset_table_length = rl16(c->buf + 0x6A0);
    c->num_offsets = offset_table_length / 4;
    if (c->num_offsets <= 0 || c->num_offsets > c->frame_count) goto fail;

    c->frame_offsets = malloc((size_t)c->num_offsets * sizeof(*c->frame_offsets));
    if (!c->frame_offsets) goto fail;
    for (n = 0; n < c->num_offsets; n++) {
        long entry = 0x6A8 + (long)n * 4, ptr;
        if (entry + 4 > c->size) goto fail;
        ptr = 0x6A8 + (long)offset_table_length + rl32(c->buf + entry);
        if (ptr >= c->size) goto fail;
        c->frame_offsets[n] = ptr;
    }
    if (c->frame_count > c->num_offsets)
        c->frame_count = c->num_offsets;

    c->layer[0] = calloc(1, PPM_PIXELS);
    c->layer[1] = calloc(1, PPM_PIXELS);
    c->prev[0]  = calloc(1, PPM_PIXELS);
    c->prev[1]  = calloc(1, PPM_PIXELS);
    if (!c->layer[0] || !c->layer[1] || !c->prev[0] || !c->prev[1]) goto fail;

    ppm_build_audio(c, frame_speed, bgm_speed);

    out->w = PPM_W; out->h = PPM_H;
    out->frame_count = c->frame_count;
    out->fps = PPM_FRAMERATES[frame_speed];
    out->audio = c->audio;
    out->audio_samples = c->audio_len;
    out->sample_rate = PPM_SAMPLE_RATE;
    out->channels = 1;
    out->get_frame = ppm_get_frame;
    out->close = ppm_close;
    out->loops = 1;                 /* Flipnotes loop */
    out->priv = c;
    return 0;

fail:
    free(c->buf); free(c->frame_offsets);
    free(c->layer[0]); free(c->layer[1]);
    free(c->prev[0]); free(c->prev[1]);
    free(c->audio);
    free(c);
    return -1;
}
