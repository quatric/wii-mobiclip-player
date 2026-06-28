#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H
#include <stdint.h>

void audio_out_start(int sample_rate);
/* push interleaved stereo int16 frames (nsamples = per-channel count) */
void audio_out_push(const int16_t *stereo, int nsamples);
int audio_out_space(void);
int  audio_out_queued(void);   /* per-channel samples buffered */
void audio_out_stop(void);

#endif
