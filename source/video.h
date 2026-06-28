#ifndef VIDEO_OUT_H
#define VIDEO_OUT_H
#include "mobi_dec.h"

void vid_init(void);
int  vid_screen_w(void);
int  vid_screen_h(void);
void vid_flip(void);
void *vid_xfb(void);

/* Clear the active framebuffer to black. */
void vid_clear(void);
/* Clear BOTH framebuffers to black (call once before a playback loop). */
void vid_clear_both(void);

/* Display refresh in Hz (50 for PAL, 60 otherwise) for vsync-count pacing. */
int  vid_refresh_hz(void);
/* Wait for one video retrace. */
void vid_vsync(void);
/* Monotonic count of video retraces since init (for absolute frame pacing). */
unsigned vid_retraces(void);

/* Blit a decoded YCgCo 4:2:0 frame, aspect-fit centered, to active XFB. */
void vid_draw_frame(MoFrame *f);

/* Blit a packed RGB24 frame (w*h*3), aspect-fit centered, to active XFB. */
void vid_draw_rgb24(const uint8_t *rgb, int w, int h);

/* Simple text console helpers (uses libogc console on a separate FB). */
void con_init(void);
void con_clear(void);
void con_printf(const char *fmt, ...);
void con_goto(int row, int col);

#endif
