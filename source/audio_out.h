#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H
#include <stdint.h>

void audio_out_start(int sample_rate);
/*
 * Queue as many interleaved stereo int16 frames as currently fit.
 * Returns the number accepted; this function never waits for video retraces.
 */
int audio_out_push(const int16_t *stereo, int nsamples);
int audio_out_space(void);
int  audio_out_queued(void);   /* per-channel samples buffered */
void audio_out_stop(void);

#endif
