#include <gccore.h>
#include <ogcsys.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>
#include "video.h"

static GXRModeObj *rmode;
static void *xfb[2];
static int cur = 0;
static int con_fb = 0;

static vu32 g_vsync = 0;
static void retrace_cb(u32 count) { (void)count; g_vsync++; }
unsigned vid_retraces(void) { return g_vsync; }

void vid_init(void)
{
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_Configure(rmode);
    VIDEO_SetPreRetraceCallback(retrace_cb);
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
    cur = 0;
}

int vid_screen_w(void) { return rmode->fbWidth; }
int vid_screen_h(void) { return rmode->xfbHeight; }
void *vid_xfb(void) { return xfb[cur]; }

void vid_flip(void)
{
    VIDEO_SetNextFramebuffer(xfb[cur]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    cur ^= 1;
}

void vid_clear(void)
{
    uint32_t *p = (uint32_t *)xfb[cur];
    int n = rmode->fbWidth * rmode->xfbHeight / 2;
    uint32_t black = 0x10801080;     /* Y=16 Cb=128 Y=16 Cr=128 */
    for (int i = 0; i < n; i++) p[i] = black;
}

void vid_clear_both(void)
{
    int n = rmode->fbWidth * rmode->xfbHeight / 2;
    uint32_t black = 0x10801080;
    for (int b = 0; b < 2; b++) {
        uint32_t *p = (uint32_t *)xfb[b];
        for (int i = 0; i < n; i++) p[i] = black;
    }
}

int vid_refresh_hz(void)
{
    int fmt = rmode->viTVMode >> 2;
    /* VI_PAL (1) and VI_DEBUG_PAL (4) are 50 Hz; the rest run at ~60 Hz. */
    return (fmt == VI_PAL || fmt == VI_DEBUG_PAL) ? 50 : 60;
}

void vid_vsync(void) { VIDEO_WaitVSync(); }

/* --- text console (separate framebuffer) ------------------------------ */
void con_init(void)
{
    con_fb = cur;
    CON_Init(xfb[con_fb], 20, 20, rmode->fbWidth, rmode->xfbHeight,
             rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_SetNextFramebuffer(xfb[con_fb]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
}
void con_clear(void)  { printf("\x1b[2J"); }
void con_goto(int row, int col) { printf("\x1b[%d;%dH", row + 1, col + 1); }
void con_printf(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
}

/* --- chroma 4:2:0 -> RGB ---------------------------------------------- */
static inline int clamp255(int v){ return v < 0 ? 0 : (v > 255 ? 255 : v); }

/* Decode one pixel to RGB given luma Y and centered chroma U,V (-128).
 * moflex==0: YCgCo  R=Y+U-V, G=Y+V, B=Y-U-V
 * moflex==1: BT.601 limited-range YCbCr (matches reference Moflex3DS path) */
static inline void to_rgb(int moflex, int Y, int U, int V, int *r, int *g, int *b)
{
    if (!moflex) {
        *r = clamp255(Y + U - V);
        *g = clamp255(Y + V);
        *b = clamp255(Y - U - V);
    } else {
        int R = Y + ((363 * V) >> 8);
        int G = Y - ((88 * U) >> 8) - ((183 * V) >> 8);
        int B = Y + ((453 * U) >> 8);
        /* limited (16..235) -> full range: (x-16)*255/239 ~= (x-16)*273>>8 */
        R = ((R - 16) * 273) >> 8;
        G = ((G - 16) * 273) >> 8;
        B = ((B - 16) * 273) >> 8;
        *r = clamp255(R); *g = clamp255(G); *b = clamp255(B);
    }
}

/* Convert two adjacent source pixels (Y0,Y1 share chroma U,V) to one YUY2 word. */
static inline uint32_t pair_to_yuy2(int moflex, int y0, int y1, int u, int v)
{
    int r0,g0,b0,r1,g1,b1;
    to_rgb(moflex, y0, u, v, &r0,&g0,&b0);
    to_rgb(moflex, y1, u, v, &r1,&g1,&b1);
    int Y0 = (77*r0 + 150*g0 + 29*b0) >> 8;
    int Y1 = (77*r1 + 150*g1 + 29*b1) >> 8;
    int Cb = ((-43*(r0+r1) - 85*(g0+g1) + 128*(b0+b1)) >> 9) + 128;
    int Cr = ((128*(r0+r1) - 107*(g0+g1) - 21*(b0+b1)) >> 9) + 128;
    return (clamp255(Y0) << 24) | (clamp255(Cb) << 16) |
           (clamp255(Y1) << 8)  |  clamp255(Cr);
}

void vid_draw_frame(MoFrame *f)
{
    uint32_t *dst = (uint32_t *)xfb[cur];
    int SW = rmode->fbWidth;
    int SH = rmode->xfbHeight;
    uint8_t *Y = f->data[0], *U = f->data[1], *V = f->data[2];
    int ly = f->linesize[0], lc = f->linesize[1];

    /* Fast path: frame fits the screen -> 1:1 centered, no scaling/divides. */
    if (f->width <= SW && f->height <= SH) {
        int w = f->width & ~1;
        int ox = (SW - w) / 2, oy = (SH - f->height) / 2;
        for (int y = 0; y < f->height; y++) {
            uint8_t *yr = Y + y * ly;
            uint8_t *ur = U + (y >> 1) * lc;
            uint8_t *vr = V + (y >> 1) * lc;
            uint32_t *drow = dst + ((oy + y) * SW + ox) / 2;
            for (int x = 0; x < w; x += 2) {
                int c = x >> 1;
                drow[c] = pair_to_yuy2(f->moflex, yr[x], yr[x+1],
                                       ur[c] - 128, vr[c] - 128);
            }
        }
        return;
    }

    /* Slow path: aspect-fit scaling (frame larger than the screen). */
    int dw = SW, dh = (int)((long)SW * f->height / f->width);
    if (dh > SH) { dh = SH; dw = (int)((long)SH * f->width / f->height); }
    dw &= ~1;
    int ox = (SW - dw) / 2, oy = (SH - dh) / 2;
    for (int y = 0; y < dh; y++) {
        int sy = (int)((long)y * f->height / dh);
        uint8_t *yr = Y + sy * ly;
        uint8_t *ur = U + (sy >> 1) * lc;
        uint8_t *vr = V + (sy >> 1) * lc;
        uint32_t *drow = dst + ((oy + y) * SW + ox) / 2;
        for (int x = 0; x < dw; x += 2) {
            int sx0 = (int)((long)x * f->width / dw);
            int sx1 = (int)((long)(x + 1) * f->width / dw);
            int y0 = yr[sx0], y1 = yr[sx1];
            int u = ur[sx0 >> 1] - 128, v = vr[sx0 >> 1] - 128;
            drow[x >> 1] = pair_to_yuy2(f->moflex, y0, y1, u, v);
        }
    }
}

void vid_draw_rgb24(const uint8_t *rgb, int w, int h)
{
    uint32_t *dst = (uint32_t *)xfb[cur];
    int SW = rmode->fbWidth;
    int SH = rmode->xfbHeight;

    int dw = SW, dh = (int)((long)SW * h / w);
    if (dh > SH) { dh = SH; dw = (int)((long)SH * w / h); }
    dw &= ~1;
    int ox = (SW - dw) / 2;
    int oy = (SH - dh) / 2;

    for (int y = 0; y < dh; y++) {
        int sy = (int)((long)y * h / dh);
        const uint8_t *srow = rgb + sy * w * 3;
        uint32_t *drow = dst + ((oy + y) * SW + ox) / 2;

        for (int x = 0; x < dw; x += 2) {
            int sx0 = (int)((long)x * w / dw);
            int sx1 = (int)((long)(x + 1) * w / dw);
            const uint8_t *p0 = srow + sx0 * 3;
            const uint8_t *p1 = srow + sx1 * 3;
            int r0 = p0[0], g0 = p0[1], b0 = p0[2];
            int r1 = p1[0], g1 = p1[1], b1 = p1[2];

            int Y0 = (77*r0 + 150*g0 + 29*b0) >> 8;
            int Y1 = (77*r1 + 150*g1 + 29*b1) >> 8;
            int Cb = ((-43*(r0+r1) - 85*(g0+g1) + 128*(b0+b1)) >> 9) + 128;
            int Cr = ((128*(r0+r1) - 107*(g0+g1) - 21*(b0+b1)) >> 9) + 128;

            drow[x >> 1] = (clamp255(Y0) << 24) | (clamp255(Cb) << 16) |
                           (clamp255(Y1) << 8)  |  clamp255(Cr);
        }
    }
}
