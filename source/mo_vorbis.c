#include <stdlib.h>
#include <string.h>
#include <tremor/ivorbiscodec.h>
#include "mo_vorbis.h"

struct MoVorbis {
    vorbis_info      vi;
    vorbis_comment   vc;
    vorbis_dsp_state vd;
    vorbis_block     vb;
    int              ready;
    int              packetno;
};

static int feed_header(MoVorbis *v, uint8_t *data, int size, int first)
{
    ogg_packet op;
    memset(&op, 0, sizeof(op));
    op.packet  = data;
    op.bytes   = size;
    op.b_o_s   = first;
    op.packetno = v->packetno++;
    return vorbis_synthesis_headerin(&v->vi, &v->vc, &op);
}

MoVorbis *mo_vorbis_open(MoDemux *m)
{
    if (!m->vh[0] || !m->vh[1] || !m->vh[2]) return NULL;
    MoVorbis *v = calloc(1, sizeof(*v));
    if (!v) return NULL;

    vorbis_info_init(&v->vi);
    vorbis_comment_init(&v->vc);

    if (feed_header(v, m->vh[0], m->vh_size[0], 1) < 0) goto fail;
    if (feed_header(v, m->vh[1], m->vh_size[1], 0) < 0) goto fail;
    if (feed_header(v, m->vh[2], m->vh_size[2], 0) < 0) goto fail;

    if (vorbis_synthesis_init(&v->vd, &v->vi) != 0) goto fail;
    vorbis_block_init(&v->vd, &v->vb);
    v->ready = 1;
    return v;
fail:
    vorbis_comment_clear(&v->vc);
    vorbis_info_clear(&v->vi);
    free(v);
    return NULL;
}

void mo_vorbis_close(MoVorbis *v)
{
    if (!v) return;
    if (v->ready) {
        vorbis_block_clear(&v->vb);
        vorbis_dsp_clear(&v->vd);
    }
    vorbis_comment_clear(&v->vc);
    vorbis_info_clear(&v->vi);
    free(v);
}

int mo_vorbis_channels(MoVorbis *v) { return v->vi.channels; }

static inline int clip15(int v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}

/* decode a single raw vorbis audio packet, append to out, return samples/ch */
static int decode_one(MoVorbis *v, const uint8_t *data, int size, int16_t *out, int produced, int out_cap_per_ch, int *bytes_consumed)
{
    ogg_packet op;
    memset(&op, 0, sizeof(op));
    op.packet   = (unsigned char *)data;
    op.bytes    = size;
    op.packetno = v->packetno++;
    op.granulepos = -1;

    if (vorbis_synthesis(&v->vb, &op) == 0) {
        vorbis_synthesis_blockin(&v->vd, &v->vb);
        
        if (bytes_consumed) {
            *bytes_consumed = (int)(v->vb.opb.ptr - v->vb.opb.buffer) + (v->vb.opb.endbit > 0 ? 1 : 0);
        }
    } else {
        if (bytes_consumed) *bytes_consumed = 0;
        return produced;
    }

    int ch = v->vi.channels;
    ogg_int32_t **pcm;
    int samples;
    while ((samples = vorbis_synthesis_pcmout(&v->vd, &pcm)) > 0) {
        int n = samples;
        if (produced + n > out_cap_per_ch) n = out_cap_per_ch - produced;
        for (int i = 0; i < n; i++)
            for (int c = 0; c < ch; c++)
                out[(produced + i) * ch + c] = (int16_t)clip15(pcm[c][i] >> 9);
        produced += n;
        vorbis_synthesis_read(&v->vd, samples);
        if (produced >= out_cap_per_ch) break;
    }
    return produced;
}

static unsigned rl16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static unsigned rl32(const uint8_t *p) { return p[0] | (p[1]<<8) | (p[2]<<16) | ((unsigned)p[3]<<24); }

int mo_vorbis_decode(MoVorbis *v, const uint8_t *data, int size,
                     int16_t *out, int out_cap_per_ch)
{
    if (!v->ready || size <= 7) return 0;   /* spurious empty marker */

    unsigned seq = rl16(data);
    int produced = 0;

    if (seq == 0xFFFF) {
        /* multi-packet: [LE16 0xFFFF? -> num at data+2][ (LE32 size, data)* ] */
        unsigned num = rl16(data + 2);
        int off = 4;
        for (unsigned i = 0; i < num && off + 4 <= size; i++) {
            unsigned psz = rl32(data + off);
            off += 4;
            if (psz == 0 || off + (int)psz > size) break;
            produced = decode_one(v, data + off, psz, out, produced, out_cap_per_ch, NULL);
            off += psz;
            if (produced >= out_cap_per_ch) break;
        }
    } else {
        /* retail single section: 4-byte [seq][offset] prefix then one or MORE vorbis
         * packets spanning the rest. Consume them sequentially. */
        int off = 4;
        while (off < size) {
            int consumed = 0;
            produced = decode_one(v, data + off, size - off, out, produced, out_cap_per_ch, &consumed);
            if (consumed <= 0) break;
            off += consumed;
            if (produced >= out_cap_per_ch) break;
        }
    }
    return produced;
}
