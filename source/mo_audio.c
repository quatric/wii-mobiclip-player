#include <string.h>
#include <math.h>
#include "mo_audio.h"
#include "mo_demux.h"

static const int16_t ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
    209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
    796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
    7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767
};
static const int8_t ima_index[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8
};

static inline int clip16(int v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}

static inline int expand_nibble(int *predictor, int *step_index, int nibble)
{
    int step = ima_step[*step_index];
    int idx  = *step_index + ima_index[nibble & 0x0F];
    if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
    int sign  = nibble & 8;
    int delta = nibble & 7;
    int diff  = ((2 * delta + 1) * step) >> 3;
    int p = *predictor;
    p = sign ? p - diff : p + diff;
    p = clip16(p);
    *predictor = p;
    *step_index = idx;
    return p;
}

#define FFMIN(a,b) ((a)<(b)?(a):(b))

static float int2float(int32_t i)
{
    union { int32_t i; float f; } u;
    u.i = i;
    return u.f;
}

static void fastaudio_setup(MoAudio *a)
{
    float (*t)[64] = a->fa_table;
    for (int i = 0; i < 8; i++)  t[0][i] = (i - 159.5f) / 160.f;
    for (int i = 0; i < 11; i++) t[0][i + 8] = (i - 37.5f) / 40.f;
    for (int i = 0; i < 27; i++) t[0][i + 8 + 11] = (i - 13.f) / 20.f;
    for (int i = 0; i < 11; i++) t[0][i + 8 + 11 + 27] = (i + 27.5f) / 40.f;
    for (int i = 0; i < 7; i++)  t[0][i + 8 + 11 + 27 + 11] = (i + 152.5f) / 160.f;
    memcpy(t[1], t[0], sizeof(t[0]));
    for (int i = 0; i < 7; i++)  t[2][i] = (i - 33.5f) / 40.f;
    for (int i = 0; i < 25; i++) t[2][i + 7] = (i - 13.f) / 20.f;
    for (int i = 0; i < 32; i++) t[3][i] = -t[2][31 - i];
    for (int i = 0; i < 16; i++) t[4][i] = i * 0.22f / 3.f - 0.6f;
    for (int i = 0; i < 16; i++) t[5][i] = i * 0.20f / 3.f - 0.3f;
    for (int i = 0; i < 8; i++)  t[6][i] = i * 0.36f / 3.f - 0.4f;
    for (int i = 0; i < 8; i++)  t[7][i] = i * 0.34f / 3.f - 0.2f;
    a->fa_ready = 1;
}

void mo_audio_init(MoAudio *a, int type, int channels)
{
    memset(a, 0, sizeof(*a));
    a->type = type;
    a->channels = channels;
    if (type == MO_AUDIO_FASTAUDIO || type == MO_AUDIO_FASTAUDIO_STEREO)
        fastaudio_setup(a);
}

static int fa_read_bits(int nbits, int *ppos, const unsigned *src)
{
    int pos = *ppos + nbits;
    int r = src[(pos - 1) / 32] >> ((-pos) & 31);
    *ppos = pos;
    return r & ((1 << nbits) - 1);
}

static const uint8_t fa_bits[8] = { 6, 6, 5, 5, 4, 0, 3, 3 };

/* decode FastAudio packet -> interleaved int16, returns samples per channel */
static int fastaudio_decode(MoAudio *a, const uint8_t *data, int size,
                            int16_t *out, int out_cap_per_ch)
{
    int ch = a->channels;
    int subframes = size / (40 * ch);
    int produced = 0;
    const uint8_t *p = data;

    for (int sf = 0; sf < subframes; sf++) {
        float chres[2][256];
        for (int channel = 0; channel < ch; channel++) {
            FastAudioChannel *c = &a->fa_ch[channel];
            float result[256] = { 0 };
            unsigned src[10];
            int inds[4], pads[4];
            float m[8];
            int pos = 0;

            for (int i = 0; i < 10; i++) {
                src[i] = p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
                p += 4;
            }
            for (int i = 0; i < 8; i++)
                m[7 - i] = a->fa_table[i][fa_read_bits(fa_bits[i], &pos, src)];
            for (int i = 0; i < 4; i++) inds[3 - i] = fa_read_bits(6, &pos, src);
            for (int i = 0; i < 4; i++) pads[3 - i] = fa_read_bits(2, &pos, src);

            for (int i = 0, index5 = 0; i < 4; i++) {
                float value = int2float((inds[i] + 1) << 20) * powf(2.f, 116.f);
                for (int j = 0, tmp = 0; j < 21; j++) {
                    int v = (j == 20) ? tmp / 2 : fa_read_bits(3, &pos, src);
                    result[i * 64 + pads[i] + j * 3] = value * (2 * v - 7);
                    if (j % 10 == 9) tmp = 4 * tmp + fa_read_bits(2, &pos, src);
                    if (j == 20)     index5 = FFMIN(2 * index5 + tmp % 2, 63);
                }
                m[2] = a->fa_table[5][index5];
            }

            for (int i = 0; i < 256; i++) {
                float x = result[i];
                for (int j = 0; j < 8; j++) {
                    x -= m[j] * c->f[j];
                    c->f[j] += m[j] * x;
                }
                memmove(&c->f[0], &c->f[1], sizeof(float) * 7);
                c->f[7] = x;
                c->last = x + c->last * 0.86f;
                result[i] = c->last * 2.f;
            }
            memcpy(chres[channel], result, sizeof(result));
        }

        for (int i = 0; i < 256; i++) {
            if (produced >= out_cap_per_ch) return produced;
            for (int channel = 0; channel < ch; channel++) {
                int s = (int)lrintf(chres[channel][i] * 32768.f);
                if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
                out[produced * ch + channel] = (int16_t)s;
            }
            produced++;
        }
    }
    return produced;
}

int mo_audio_decode(MoAudio *a, const uint8_t *data, int size,
                    int16_t *out, int out_cap_per_ch)
{
    int ch = a->channels;

    if (a->type == MO_AUDIO_PCM) {
        int n = size / (2 * ch);
        if (n > out_cap_per_ch) n = out_cap_per_ch;
        for (int i = 0; i < n * ch; i++)
            out[i] = (int16_t)(data[i * 2] | (data[i * 2 + 1] << 8));
        return n;
    }

    if (a->type == MO_AUDIO_ADPCM || a->type == MO_AUDIO_ADPCM_STEREO) {
        int produced = 0;            /* per channel */
        int pos = 0;
        /* subframe = ch * 132 bytes; 256 samples/ch per subframe */
        while (pos + ch * 132 <= size) {
            int16_t tmp[2][256];
            for (int c = 0; c < ch; c++) {
                int si = (int16_t)(data[pos] | (data[pos + 1] << 8));
                int pr = (int16_t)(data[pos + 2] | (data[pos + 3] << 8));
                pos += 4;
                if (si < 0) si = 0; else if (si > 88) si = 88;
                a->step_index[c] = si;
                a->predictor[c] = pr;
                for (int n = 0; n < 256; n += 2) {
                    int v = data[pos++];
                    tmp[c][n]     = expand_nibble(&a->predictor[c], &a->step_index[c], v & 0x0F);
                    tmp[c][n + 1] = expand_nibble(&a->predictor[c], &a->step_index[c], v >> 4);
                }
            }
            for (int n = 0; n < 256; n++) {
                if (produced >= out_cap_per_ch) return produced;
                for (int c = 0; c < ch; c++)
                    out[produced * ch + c] = tmp[c][n];
                produced++;
            }
        }
        return produced;
    }

    if (a->type == MO_AUDIO_FASTAUDIO || a->type == MO_AUDIO_FASTAUDIO_STEREO)
        return fastaudio_decode(a, data, size, out, out_cap_per_ch);

    return 0;
}
