/*
 * Flipnote Studio 3D (KWZ) decoder — standalone port of FFmpeg
 * libavformat/kwzdec.c (LGPL) for the Wii player. Reads the whole file into
 * memory, decodes frames to RGB24 on demand (persistent layer buffers, so
 * frames must be requested in order), and pre-decodes/mixes all sound tracks
 * into one mono int16 master at 32768 Hz. Ported from flipnote.js.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rgb_source.h"

#define KWZ_W 320
#define KWZ_H 240
#define KWZ_PIXELS (KWZ_W * KWZ_H)

#define KWZ_RAW_SAMPLE_RATE 16364
#define KWZ_SAMPLE_RATE     32768

/* ---- little-endian / clip helpers ---- */
static inline uint16_t rl16(const uint8_t *p){ return p[0] | (p[1] << 8); }
static inline uint32_t rl32(const uint8_t *p){
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static inline int clipi(int v, int lo, int hi){ return v < lo ? lo : (v > hi ? hi : v); }
static inline int16_t clip16(int v){ return v < -32768 ? -32768 : (v > 32767 ? 32767 : v); }

static const int8_t ADPCM_INDEX_TABLE_2BIT[4] = { -1, 2, -1, 2 };
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

static const float KWZ_FRAMERATES[11] = { .2f, .5f, 1, 2, 4, 6, 8, 12, 20, 24, 30 };

/* palette: WHITE, BLACK, RED, YELLOW, GREEN, BLUE, NONE(transparent) */
static const uint8_t KWZ_RGB[7][3] = {
    { 0xff, 0xff, 0xff }, { 0x10, 0x10, 0x10 }, { 0xff, 0x10, 0x10 },
    { 0xff, 0xe7, 0x00 }, { 0x00, 0x86, 0x31 }, { 0x00, 0x38, 0xce },
    { 0xff, 0xff, 0xff },
};

static const uint16_t KWZ_COMMON_IDX[32] = {
    0x0000, 0x0CD0, 0x19A0, 0x02D9, 0x088B, 0x0051, 0x00F3, 0x0009,
    0x001B, 0x0001, 0x0003, 0x05B2, 0x1116, 0x00A2, 0x01E6, 0x0012,
    0x0036, 0x0002, 0x0006, 0x0B64, 0x08DC, 0x0144, 0x00FC, 0x0024,
    0x001C, 0x0004, 0x0334, 0x099C, 0x0668, 0x1338, 0x1004, 0x166C
};

typedef struct KwzCtx {
    uint8_t *buf;
    long     size;

    int      frame_count;
    int      frame;             /* next frame to decode (sequential guard) */

    uint32_t *meta_off;
    uint32_t *data_off;
    uint16_t (*layer_sz)[3];

    uint8_t *line;              /* 6561 * 8 */
    uint8_t *line_shift;
    uint8_t common[32 * 8];
    uint8_t common_shift[32 * 8];

    uint8_t *layer[3];

    /* bit reader */
    const uint8_t *bp, *bend;
    uint32_t bit_value;
    int      bit_index;

    int16_t *audio;
    int      audio_len;
    double   samples_per_frame;
} KwzCtx;

static void build_line_tables(KwzCtx *c)
{
    int a, b, d, e, f, g, h, i, off = 0;
    for (a = 0; a < 3; a++)
    for (b = 0; b < 3; b++)
    for (d = 0; d < 3; d++)
    for (e = 0; e < 3; e++)
    for (f = 0; f < 3; f++)
    for (g = 0; g < 3; g++)
    for (h = 0; h < 3; h++)
    for (i = 0; i < 3; i++) {
        uint8_t A=a,B=b,C=d,D=e,E=f,F=g,G=h,H=i;
        uint8_t *l  = c->line       + off;
        uint8_t *ls = c->line_shift + off;
        l[0]=B; l[1]=A; l[2]=D; l[3]=C; l[4]=F; l[5]=E; l[6]=H; l[7]=G;
        ls[0]=A; ls[1]=D; ls[2]=C; ls[3]=F; ls[4]=E; ls[5]=H; ls[6]=G; ls[7]=B;
        off += 8;
    }
    for (i = 0; i < 32; i++) {
        memcpy(c->common       + i*8, c->line       + KWZ_COMMON_IDX[i]*8, 8);
        memcpy(c->common_shift + i*8, c->line_shift + KWZ_COMMON_IDX[i]*8, 8);
    }
}

static int find_section(KwzCtx *c, const char *magic, long *data_ptr, uint32_t *length)
{
    long ptr = 0;
    int count = 0;
    while (ptr + 8 <= c->size && count < 6) {
        uint32_t len = rl32(c->buf + ptr + 4);
        if (c->buf[ptr]==magic[0] && c->buf[ptr+1]==magic[1] && c->buf[ptr+2]==magic[2]) {
            if (data_ptr) *data_ptr = ptr + 8;
            if (length)   *length   = len;
            return 0;
        }
        ptr += (long)len + 8;
        count++;
    }
    return -1;
}

static inline unsigned read_bits(KwzCtx *c, int num)
{
    unsigned res;
    if (c->bit_index + num > 16) {
        uint16_t nb = (c->bp + 1 < c->bend) ? rl16(c->bp) : 0;
        c->bp += 2;
        c->bit_value |= (uint32_t)nb << (16 - c->bit_index);
        c->bit_index -= 16;
    }
    res = c->bit_value & ((1u << num) - 1);
    c->bit_value >>= num;
    c->bit_index += num;
    return res;
}

static void put_line(uint8_t *dst, const uint8_t *pix){ memcpy(dst, pix, 8); }

static void decode_layer(KwzCtx *c, int layer_index, int frame_index)
{
    uint8_t *pixel_buffer = c->layer[layer_index];
    const uint8_t *base = c->buf + c->data_off[frame_index];
    uint16_t layer_size;
    int skip = 0, tox, toy, sx, sy, x, y, off = 0, li;

    for (li = 0; li < layer_index; li++)
        off += c->layer_sz[frame_index][li];
    layer_size = c->layer_sz[frame_index][layer_index];
    if (layer_size == 38) return;   /* unchanged */

    c->bp = base + off;
    c->bend = base + off + layer_size;
    c->bit_index = 16;
    c->bit_value = 0;

    for (toy = 0; toy < 240; toy += 128) {
        for (tox = 0; tox < 320; tox += 128) {
            for (sy = 0; sy < 128; sy += 8) {
                y = toy + sy;
                if (y >= 240) break;
                for (sx = 0; sx < 128; sx += 8) {
                    int ptr, tile_type;
                    x = tox + sx;
                    if (x >= 320) break;
                    if (skip > 0) { skip--; continue; }
                    ptr = y * KWZ_W + x;
                    tile_type = read_bits(c, 3);

                    if (tile_type == 0) {
                        const uint8_t *px = c->common + read_bits(c, 5) * 8;
                        for (int k = 0; k < 8; k++, ptr += KWZ_W)
                            put_line(pixel_buffer + ptr, px);
                    } else if (tile_type == 1) {
                        const uint8_t *px = c->line + read_bits(c, 13) * 8;
                        for (int k = 0; k < 8; k++, ptr += KWZ_W)
                            put_line(pixel_buffer + ptr, px);
                    } else if (tile_type == 2) {
                        int lp = read_bits(c, 5) * 8;
                        const uint8_t *pa = c->common + lp, *pb = c->common_shift + lp;
                        for (int k = 0; k < 8; k++, ptr += KWZ_W)
                            put_line(pixel_buffer + ptr, (k & 1) ? pb : pa);
                    } else if (tile_type == 3) {
                        int lp = read_bits(c, 13) * 8;
                        const uint8_t *pa = c->line + lp, *pb = c->line_shift + lp;
                        for (int k = 0; k < 8; k++, ptr += KWZ_W)
                            put_line(pixel_buffer + ptr, (k & 1) ? pb : pa);
                    } else if (tile_type == 4) {
                        int flags = read_bits(c, 8), mask;
                        for (mask = 1; mask < 0xFF; mask <<= 1) {
                            const uint8_t *px = (flags & mask)
                                ? c->common + read_bits(c, 5) * 8
                                : c->line   + read_bits(c, 13) * 8;
                            put_line(pixel_buffer + ptr, px);
                            ptr += KWZ_W;
                        }
                    } else if (tile_type == 5) {
                        skip = read_bits(c, 5);
                        continue;
                    } else if (tile_type == 7) {
                        int pattern = read_bits(c, 2);
                        int use_common = read_bits(c, 1);
                        const uint8_t *pa, *pb, *row;
                        static const uint8_t seq[4][8] = {
                            { 0,1,0,1,0,1,0,1 }, { 0,0,1,0,0,1,0,0 },
                            { 0,1,0,0,1,0,0,1 }, { 0,1,1,0,1,1,0,1 },
                        };
                        if (use_common) {
                            int la = read_bits(c, 5)*8, lb = read_bits(c, 5)*8;
                            pa = c->common + la; pb = c->common + lb; pattern += 1;
                        } else {
                            int la = read_bits(c, 13)*8, lb = read_bits(c, 13)*8;
                            pa = c->line + la; pb = c->line + lb;
                        }
                        row = seq[pattern & 3];
                        for (int k = 0; k < 8; k++, ptr += KWZ_W)
                            put_line(pixel_buffer + ptr, row[k] ? pb : pa);
                    }
                }
            }
        }
    }
}

/* KWZ ADPCM (variable 2-bit / 4-bit IMA) -> malloc'd int16 PCM */
static int16_t *kwz_decode_track(const uint8_t *src, int src_size, int *out_len)
{
    int16_t *dst;
    int predictor = 0, step_index = 40, dst_ptr = 0, i;
    *out_len = 0;
    if (src_size <= 0) return NULL;
    dst = malloc((size_t)src_size * 8 * sizeof(*dst));
    if (!dst) return NULL;
    for (i = 0; i < src_size; i++) {
        int cur_byte = src[i], cur_bit = 0;
        while (cur_bit < 8) {
            int sample, step, diff;
            if (step_index < 18 || cur_bit > 4) {
                sample = cur_byte & 0x3;
                step = ADPCM_STEP_TABLE[step_index];
                diff = step >> 3;
                if (sample & 1) diff += step;
                if (sample & 2) diff = -diff;
                predictor += diff;
                step_index += ADPCM_INDEX_TABLE_2BIT[sample];
                cur_byte >>= 2; cur_bit += 2;
            } else {
                sample = cur_byte & 0xf;
                step = ADPCM_STEP_TABLE[step_index];
                diff = step >> 3;
                if (sample & 1) diff += step >> 2;
                if (sample & 2) diff += step >> 1;
                if (sample & 4) diff += step;
                if (sample & 8) diff = -diff;
                predictor += diff;
                step_index += ADPCM_INDEX_TABLE_4BIT[sample];
                cur_byte >>= 4; cur_bit += 4;
            }
            step_index = clipi(step_index, 0, 79);
            predictor  = clipi(predictor, -2048, 2047);
            dst[dst_ptr++] = predictor * 16;
        }
    }
    *out_len = dst_ptr;
    return dst;
}

static int16_t *kwz_resample_linear(const int16_t *src, int src_len,
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
        double a = i * adj;
        int sp = (int)a;
        double w = a - sp;
        int s0 = (sp     >= 0 && sp     < src_len) ? src[sp]     : 0;
        int s1 = (sp + 1 >= 0 && sp + 1 < src_len) ? src[sp + 1] : 0;
        dst[i] = (int16_t)lrint(s0 + w * (s1 - s0));
    }
    *out_len = dst_len;
    return dst;
}

static void kwz_mix_full(int16_t *dst, int dst_len, const int16_t *src, int src_len, int off)
{
    for (int n = 0; n < src_len; n++) {
        int idx = off + n;
        if (idx >= dst_len) break;
        dst[idx] = clip16(dst[idx] + src[n]);
    }
}

static void kwz_build_audio(KwzCtx *c, int frame_speed)
{
    const int dst_freq = KWZ_SAMPLE_RATE;
    double framerate = KWZ_FRAMERATES[frame_speed];
    double duration = c->frame_count / framerate;
    int master_len = (int)ceil(duration * dst_freq);
    long ksn, base, track_ptr[5];
    uint32_t bgm_speed, track_len[5];
    int16_t *master;
    double bgmrate;
    int t, i;

    c->samples_per_frame = (double)dst_freq / framerate;
    if (master_len <= 0) return;
    if (find_section(c, "KSN", &ksn, NULL) < 0) return;
    if (ksn + 28 > c->size) return;

    bgm_speed = rl32(c->buf + ksn);
    if (bgm_speed > 10) bgm_speed = frame_speed;
    bgmrate = KWZ_FRAMERATES[bgm_speed];

    for (t = 0; t < 5; t++)
        track_len[t] = rl32(c->buf + ksn + 4 + t * 4);
    base = ksn + 28;
    track_ptr[0] = base;
    for (t = 1; t < 5; t++)
        track_ptr[t] = track_ptr[t-1] + track_len[t-1];

    for (i = 0, t = 0; t < 5; t++) i += track_len[t];
    if (i == 0) return;

    master = calloc(master_len, sizeof(*master));
    if (!master) return;

    if (track_len[0] > 0 && track_ptr[0] + track_len[0] <= c->size) {
        int raw_len, res_len;
        int16_t *raw = kwz_decode_track(c->buf + track_ptr[0], track_len[0], &raw_len);
        if (raw) {
            double src_freq = KWZ_RAW_SAMPLE_RATE * (framerate / bgmrate);
            int16_t *r = kwz_resample_linear(raw, raw_len, src_freq, dst_freq, &res_len);
            free(raw);
            if (r) { kwz_mix_full(master, master_len, r, res_len, 0); free(r); }
        }
    }

    {
        int16_t *se[4] = { NULL, NULL, NULL, NULL };
        int se_len[4] = { 0, 0, 0, 0 };
        double spf = (double)dst_freq / framerate;
        for (t = 1; t < 5; t++) {
            if (track_len[t] > 0 && track_ptr[t] + track_len[t] <= c->size) {
                int raw_len, res_len;
                int16_t *raw = kwz_decode_track(c->buf + track_ptr[t], track_len[t], &raw_len);
                if (raw) {
                    se[t-1] = kwz_resample_linear(raw, raw_len, KWZ_RAW_SAMPLE_RATE,
                                                  dst_freq, &res_len);
                    se_len[t-1] = res_len;
                    free(raw);
                }
            }
        }
        if (se[0] || se[1] || se[2] || se[3]) {
            for (i = 0; i < c->frame_count; i++) {
                int off = (int)ceil(i * spf);
                uint8_t flag = c->buf[c->meta_off[i] + 0x17];
                for (int k = 0; k < 4; k++)
                    if (se[k] && (flag & (1 << k)))
                        kwz_mix_full(master, master_len, se[k], se_len[k], off);
            }
        }
        for (t = 0; t < 4; t++) free(se[t]);
    }

    c->audio = master;
    c->audio_len = master_len;
}

/* ---- RgbSource interface ---- */

static int kwz_get_frame(RgbSource *s, int idx, uint8_t *dst)
{
    KwzCtx *c = s->priv;
    uint32_t flags;
    int pal[7], i, layer;

    if (idx < 0 || idx >= c->frame_count) return -1;
    /* Frames must be decoded in order for the persistent layer buffers; if the
     * caller ever skips ahead, catch up silently. */
    while (c->frame < idx) {
        for (layer = 0; layer < 3; layer++)
            decode_layer(c, layer, c->frame);
        c->frame++;
    }
    for (layer = 0; layer < 3; layer++)
        decode_layer(c, layer, idx);
    c->frame = idx + 1;

    flags = rl32(c->buf + c->meta_off[idx]);
    pal[0] =  flags        & 0xF;
    pal[1] = (flags >> 8)  & 0xF;
    pal[2] = (flags >> 12) & 0xF;
    pal[3] = (flags >> 16) & 0xF;
    pal[4] = (flags >> 20) & 0xF;
    pal[5] = (flags >> 24) & 0xF;
    pal[6] = (flags >> 28) & 0xF;
    for (i = 0; i < 7; i++) if (pal[i] > 6) pal[i] = 6;

    {
        const uint8_t *paper = KWZ_RGB[pal[0]];
        for (i = 0; i < KWZ_PIXELS; i++) {
            dst[i*3+0] = paper[0]; dst[i*3+1] = paper[1]; dst[i*3+2] = paper[2];
        }
    }
    {
        static const int order[3] = { 2, 1, 0 };
        for (int o = 0; o < 3; o++) {
            int l = order[o];
            const uint8_t *buf = c->layer[l];
            for (i = 0; i < KWZ_PIXELS; i++) {
                int v = buf[i], ci;
                if (!v) continue;
                ci = pal[1 + l*2 + (v - 1)];
                if (ci == 6) continue;
                dst[i*3+0] = KWZ_RGB[ci][0];
                dst[i*3+1] = KWZ_RGB[ci][1];
                dst[i*3+2] = KWZ_RGB[ci][2];
            }
        }
    }
    return 0;
}

static void kwz_close(RgbSource *s)
{
    KwzCtx *c = s->priv;
    if (!c) return;
    free(c->buf); free(c->meta_off); free(c->data_off); free(c->layer_sz);
    free(c->line); free(c->line_shift);
    for (int i = 0; i < 3; i++) free(c->layer[i]);
    free(c->audio);
    free(c);
    s->priv = NULL;
}

int kwz_open(RgbSource *out, const char *path)
{
    KwzCtx *c;
    FILE *f;
    long sz, meta_ptr, data_ptr, kfh, kmi, kmc;
    uint32_t kmi_len, kmc_len;
    int frame_speed, i;

    memset(out, 0, sizeof(*out));
    c = calloc(1, sizeof(*c));
    if (!c) return -1;

    f = fopen(path, "rb");
    if (!f) { free(c); return -1; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); free(c); return -1; }
    c->size = sz;
    c->buf = malloc(sz);
    if (!c->buf) { fclose(f); free(c); return -1; }
    if (fread(c->buf, 1, sz, f) != (size_t)sz) { fclose(f); goto fail; }
    fclose(f);

    if (find_section(c, "KFH", &kfh, NULL) < 0) goto fail;
    if (find_section(c, "KMI", &kmi, &kmi_len) < 0 ||
        find_section(c, "KMC", &kmc, &kmc_len) < 0) goto fail;

    if (kfh + 0xCB > c->size) goto fail;
    c->frame_count = rl16(c->buf + kfh + 0xC4);
    frame_speed = c->buf[kfh + 0xCA];
    if (frame_speed > 10) frame_speed = 10;
    if (c->frame_count <= 0) goto fail;
    if ((long)c->frame_count * 28 > kmi_len) goto fail;

    c->meta_off = malloc(c->frame_count * sizeof(*c->meta_off));
    c->data_off = malloc(c->frame_count * sizeof(*c->data_off));
    c->layer_sz = malloc(c->frame_count * sizeof(*c->layer_sz));
    if (!c->meta_off || !c->data_off || !c->layer_sz) goto fail;

    meta_ptr = kmi;
    data_ptr = kmc + 4;
    for (i = 0; i < c->frame_count; i++) {
        long a;
        if (meta_ptr + 10 > c->size) goto fail;
        c->layer_sz[i][0] = rl16(c->buf + meta_ptr + 4);
        c->layer_sz[i][1] = rl16(c->buf + meta_ptr + 6);
        c->layer_sz[i][2] = rl16(c->buf + meta_ptr + 8);
        c->meta_off[i] = meta_ptr;
        c->data_off[i] = data_ptr;
        a = (long)c->layer_sz[i][0] + c->layer_sz[i][1] + c->layer_sz[i][2];
        meta_ptr += 28;
        data_ptr += a;
        if (data_ptr > c->size) goto fail;
    }

    c->line = malloc(6561 * 8);
    c->line_shift = malloc(6561 * 8);
    for (i = 0; i < 3; i++) c->layer[i] = calloc(1, KWZ_PIXELS);
    if (!c->line || !c->line_shift || !c->layer[0] || !c->layer[1] || !c->layer[2]) goto fail;
    build_line_tables(c);
    (void)kmc_len;

    kwz_build_audio(c, frame_speed);

    out->w = KWZ_W; out->h = KWZ_H;
    out->frame_count = c->frame_count;
    out->fps = KWZ_FRAMERATES[frame_speed];
    out->audio = c->audio;
    out->audio_samples = c->audio_len;
    out->sample_rate = KWZ_SAMPLE_RATE;
    out->channels = 1;
    out->get_frame = kwz_get_frame;
    out->close = kwz_close;
    out->loops = 1;                 /* Flipnotes loop */
    out->priv = c;
    return 0;

fail:
    free(c->buf); free(c->meta_off); free(c->data_off); free(c->layer_sz);
    free(c->line); free(c->line_shift);
    for (i = 0; i < 3; i++) free(c->layer[i]);
    free(c->audio);
    free(c);
    return -1;
}
