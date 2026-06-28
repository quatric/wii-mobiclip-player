/* Mobiclip audio: PCM s16 and IMA Mobiclip-Wii ADPCM -> native int16. */
#ifndef MO_AUDIO_H
#define MO_AUDIO_H

#include <stdint.h>

typedef struct FastAudioChannel {
    float f[8];
    float last;
} FastAudioChannel;

typedef struct MoAudio {
    int type;
    int channels;
    /* ADPCM running state per channel */
    int predictor[2];
    int step_index[2];
    /* FastAudio state */
    int   fa_ready;
    float fa_table[8][64];
    FastAudioChannel fa_ch[2];
} MoAudio;

void mo_audio_init(MoAudio *a, int type, int channels);

/*
 * Decode one audio packet. Writes interleaved native-endian int16 samples to
 * out (must hold at least mo_audio_max_samples(size) * channels). Returns the
 * number of samples *per channel* written, or <0 on error.
 */
int  mo_audio_decode(MoAudio *a, const uint8_t *data, int size,
                     int16_t *out, int out_cap_per_ch);

#endif
