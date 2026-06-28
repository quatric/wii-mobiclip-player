/* Ogg Vorbis (Tremor / libvorbisidec) decode for Mobiclip .mo audio. */
#ifndef MO_VORBIS_H
#define MO_VORBIS_H

#include <stdint.h>
#include "mo_demux.h"

typedef struct MoVorbis MoVorbis;

/* Initialise from the three setup headers stored in the demuxer.
 * Returns NULL on failure. */
MoVorbis *mo_vorbis_open(MoDemux *m);
void      mo_vorbis_close(MoVorbis *v);

int       mo_vorbis_channels(MoVorbis *v);

/*
 * Decode one whole audio section (as delivered by mo_demux_read for a Vorbis
 * stream: optional [LE16 seq][LE16 off] prefix or [0xFFFF][num][LE32 size]..
 * multi-packet form). Writes interleaved native int16 to out; returns samples
 * per channel produced, or <0 on error.
 */
int       mo_vorbis_decode(MoVorbis *v, const uint8_t *data, int size,
                           int16_t *out, int out_cap_per_ch);

#endif
