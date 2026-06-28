#include <stdlib.h>
#include <string.h>
#include "mo_demux.h"

/* ---- little-endian readers from FILE* -------------------------------- */
static uint32_t rl32(FILE *f)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
}
static uint16_t rl16(FILE *f)
{
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    return b[0] | (b[1] << 8);
}

#define MARK(a,b) ((a) | ((b) << 8))
#define FMT_LENGTH           MARK('T','L')
#define FMT_VIDEO            MARK('V','2')
#define FMT_RSA              MARK('p','c')
#define FMT_UNKNOWN_AUDIO    MARK('P',0xc6)
#define FMT_FASTAUDIO        MARK('A','2')
#define FMT_FASTAUDIO_STEREO MARK('A','3')
#define FMT_PCM              MARK('A','P')
#define FMT_ADPCM            MARK('A','8')
#define FMT_ADPCM_STEREO     MARK('A','9')
#define FMT_VORBIS           MARK('A','V')
#define FMT_KEYINDEX         MARK('K','I')
#define FMT_HEADER_DONE      MARK('H','E')

int mo_demux_open(MoDemux *m, const char *path)
{
    memset(m, 0, sizeof(*m));
    m->f = fopen(path, "rb");
    if (!m->f) return -1;

    uint8_t magic[4];
    if (fread(magic, 1, 4, m->f) != 4 ||
        magic[0] != 'M' || magic[1] != 'O' || magic[2] != 'C' || magic[3] != '5') {
        fclose(m->f); m->f = NULL; return -2;
    }

    long header_length = (long)rl32(m->f) + 8;
    int done = 0;

    while (!done) {
        if (ftell(m->f) > header_length) break;

        uint16_t marker = rl16(m->f);
        uint16_t flen   = rl16(m->f) * 4;
        if (ftell(m->f) + flen > header_length) break;

        switch (marker) {
        case FMT_LENGTH:
            m->fps_num = 256;
            m->fps_den = rl32(m->f);
            m->frame_count = rl32(m->f);
            rl32(m->f); /* unknown */
            break;
        case FMT_VIDEO:
            m->width  = rl32(m->f);
            m->height = rl32(m->f);
            break;
        case FMT_FASTAUDIO:
        case FMT_FASTAUDIO_STEREO:
        case FMT_PCM:
        case FMT_ADPCM:
        case FMT_ADPCM_STEREO:
            m->sample_rate = rl32(m->f);
            uint32_t ch_count = rl32(m->f); /* channel count */
            switch (marker) {
            case FMT_FASTAUDIO:        m->audio_type = MO_AUDIO_FASTAUDIO;        m->channels = 1; break;
            case FMT_FASTAUDIO_STEREO: m->audio_type = MO_AUDIO_FASTAUDIO_STEREO; m->channels = 2; break;
            case FMT_PCM:              m->audio_type = MO_AUDIO_PCM;              m->channels = ch_count ? ch_count : 2; break;
            case FMT_ADPCM:            m->audio_type = MO_AUDIO_ADPCM;            m->channels = 1; break;
            case FMT_ADPCM_STEREO:     m->audio_type = MO_AUDIO_ADPCM_STEREO;     m->channels = 2; break;
            }
            break;
        case FMT_VORBIS: {
            /* [LE32 p1_size][p1][LE32 p2_size][p2][LE32 p3_size][p3]
             * p1=identification, p2=comment, p3=setup (standard Vorbis). */
            long sec_start = ftell(m->f);
            int ok = 1;
            for (int i = 0; i < 3; i++) {
                uint32_t sz = rl32(m->f);
                if (sz == 0 || sz > 65536) { ok = 0; break; }
                m->vh[i] = malloc(sz);
                if (!m->vh[i] || fread(m->vh[i], 1, sz, m->f) != sz) { ok = 0; break; }
                m->vh_size[i] = sz;
            }
            if (ok) {
                m->audio_type = MO_AUDIO_VORBIS;
                /* channels/rate live in the identification header */
                uint8_t *p1 = m->vh[0];
                if (m->vh_size[0] >= 16 && p1[0] == 1 &&
                    !memcmp(p1 + 1, "vorbis", 6)) {
                    m->channels = p1[11];
                    /* Always trust the Vorbis ID header rate — it is the
                     * authoritative playback rate (e.g. 32000 Hz). The
                     * unknown FMT_LENGTH field must not override it. */
                    m->sample_rate = p1[12] | (p1[13]<<8) | (p1[14]<<16) | ((uint32_t)p1[15]<<24);
                } else { m->channels = 2; m->sample_rate = 48000; }
            }
            /* skip to end of this format section regardless */
            fseek(m->f, sec_start + flen, SEEK_SET);
            break;
        }
        case FMT_HEADER_DONE:
            done = 1;
            break;
        case FMT_RSA:
        case FMT_UNKNOWN_AUDIO:
        case FMT_KEYINDEX:
        default:
            fseek(m->f, flen, SEEK_CUR);
            break;
        }
    }

    if (!done) { fclose(m->f); m->f = NULL; return -3; }
    if ((m->width & 15) || (m->height & 15) || m->width <= 0 || m->height <= 0) {
        fclose(m->f); m->f = NULL; return -4;
    }

    m->data_start = ftell(m->f);
    m->cur_frame = 0;
    m->eof = 0;
    return 0;
}

/* growable read buffers, one for video one for audio */
static uint8_t *g_vbuf; static int g_vcap;
static uint8_t *g_abuf; static int g_acap;

static uint8_t *ensure(uint8_t **buf, int *cap, int need)
{
    if (need > *cap) {
        uint8_t *nb = realloc(*buf, need);
        if (!nb) return NULL;
        *buf = nb; *cap = need;
    }
    return *buf;
}

/*
 * mo_demux_read produces video then audio for each chunk. We read the whole
 * chunk on the video call and stash the audio for the following call.
 */
static uint8_t *g_pending_audio;   /* points into g_abuf */
static int      g_pending_audio_sz;
static int      g_have_pending;

void mo_demux_close(MoDemux *m)
{
    if (m->f) fclose(m->f);
    m->f = NULL;
    for (int i = 0; i < 3; i++) { free(m->vh[i]); m->vh[i] = NULL; }
    /* The pending-audio handshake is process-global scratch; clear it so a
     * second demuxer instance (e.g. the Vorbis pre-decode pass) can't inherit
     * a stale pending packet from this one. */
    g_have_pending = 0;
}

int mo_demux_read(MoDemux *m, MoPacket *pkt)
{
    if (!m->f || m->eof) return 0;

    if (g_have_pending) {
        pkt->is_audio = 1;
        pkt->data = g_pending_audio;
        pkt->size = g_pending_audio_sz;
        pkt->frame_index = m->cur_frame - 1;
        g_have_pending = 0;
        return 1;
    }

    if (m->frame_count && m->cur_frame >= m->frame_count) { m->eof = 1; return 0; }

    uint32_t chunk_size = rl32(m->f);
    uint32_t video_size = rl32(m->f);
    if (feof(m->f) || chunk_size < video_size + 8) { m->eof = 1; return 0; }
    uint32_t audio_size = chunk_size - video_size - 8;

    if (!ensure(&g_vbuf, &g_vcap, video_size ? video_size : 1)) return -1;
    if (fread(g_vbuf, 1, video_size, m->f) != video_size) { m->eof = 1; return 0; }

    /* audio + alignment padding. NB: matches FFmpeg exactly --
     * padding = 4 - (pos % 4), which is always 1..4 (a full 4 when
     * already aligned), not a no-op. */
    long pos = ftell(m->f) + audio_size;
    long padding = 4 - (pos % 4);

    /* How many bytes to actually hand to the audio decoder.
     *   Vorbis: whole section incl. the 4-byte prefix and trailing pad, so
     *           the last packet is byte-complete (Tremor self-delimits).
     *   Block codecs (ADPCM/PCM/FastAudio): round the read up into the pad
     *           to complete a partial final frame (per ffmpeg_encoder modec). */
    int read_size = audio_size;
    int block = 0;
    if (m->audio_type == MO_AUDIO_VORBIS) {
        read_size = audio_size; // Tremor fails if given trailing alignment padding
    } else if (m->audio_type == MO_AUDIO_ADPCM || m->audio_type == MO_AUDIO_ADPCM_STEREO) {
        block = m->channels * 132;
    } else if (m->audio_type == MO_AUDIO_PCM) {
        block = m->channels * 2;
    } else if (m->audio_type == MO_AUDIO_FASTAUDIO || m->audio_type == MO_AUDIO_FASTAUDIO_STEREO) {
        block = m->channels * 40;
    }
    if (block > 0) {
        int total = audio_size + padding;
        int rem = read_size % block;
        if (rem) {
            int need = block - rem;
            read_size += (need <= padding) ? need : (total - read_size);
        }
    }

    if (read_size > 0) {
        if (!ensure(&g_abuf, &g_acap, read_size)) return -1;
        int got = fread(g_abuf, 1, read_size, m->f);
        if (got != read_size) { m->eof = 1; read_size = got; }
    }
    /* skip any padding we didn't already consume into the audio read */
    long consumed_pad = read_size - (long)audio_size;
    if (consumed_pad < 0) consumed_pad = 0;
    if (padding - consumed_pad > 0) fseek(m->f, padding - consumed_pad, SEEK_CUR);

    g_pending_audio = g_abuf;
    g_pending_audio_sz = read_size;
    g_have_pending = read_size > 0;

    pkt->is_audio = 0;
    pkt->data = g_vbuf;
    pkt->size = video_size;
    pkt->frame_index = m->cur_frame;
    m->cur_frame++;
    return 1;
}
