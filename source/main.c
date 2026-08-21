#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "video.h"
#include "audio_out.h"
#include "mo_demux.h"
#include "mo_audio.h"
#include "mo_vorbis.h"
#include "mobi_dec.h"
#include "rgb_source.h"
#include "input.h"

/* ------------------------------------------------------------------ */
/* File browser                                                        */
/* ------------------------------------------------------------------ */

#define MAX_ENTRIES 512
typedef struct { char name[256]; int is_dir; } Entry;
static Entry entries[MAX_ENTRIES];
static int n_entries;

/* case-insensitive match of a dotted extension (e.g. ".mo", ".kwz") */
static int has_ext(const char *s, const char *ext)
{
    size_t n = strlen(s), e = strlen(ext);
    if (n < e) return 0;
    const char *p = s + n - e;
    for (size_t i = 0; i < e; i++) {
        char c = p[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != ext[i]) return 0;
    }
    return 1;
}

/* playable: .mo (Mobiclip), plus KWZ/PPM Flipnotes and THP */
static int is_playable(const char *s)
{
    return has_ext(s, ".mo")  || has_ext(s, ".kwz") ||
           has_ext(s, ".ppm") || has_ext(s, ".thp");
}

static int cmp_entry(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir; /* dirs first */
    return strcasecmp(x->name, y->name);
}

static void scan_dir(const char *path)
{
    n_entries = 0;
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && n_entries < MAX_ENTRIES) {
        if (e->d_name[0] == '.' && strcmp(e->d_name, "..")) continue;  /* hide dotfiles */
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        struct stat st;
        int isdir = 0;
        if (stat(full, &st) == 0) isdir = S_ISDIR(st.st_mode);
        if (!isdir && !is_playable(e->d_name)) continue;
        snprintf(entries[n_entries].name, sizeof(entries[n_entries].name),
                 "%s", e->d_name);
        entries[n_entries].is_dir = isdir;
        n_entries++;
    }
    closedir(d);
    qsort(entries, n_entries, sizeof(Entry), cmp_entry);
}

static void draw_browser(const char *path, int sel, int top)
{
    con_clear();
    con_goto(0, 0);
    con_printf("Wii Mobiclip Player  -  %s\n", path);
    con_printf("--------------------------------------------------\n");
    int rows = 22;
    for (int i = 0; i < rows; i++) {
        int idx = top + i;
        if (idx >= n_entries) break;
        con_printf(" %c %s%s\n",
                   idx == sel ? '>' : ' ',
                   entries[idx].is_dir ? "  " : "  ",
                   entries[idx].name);
    }
    con_goto(24, 0);
    con_printf("[Up/Down] move  [L/R +/-] page  [A] open  [B] up  [HOME] exit");
}

/* ------------------------------------------------------------------ */
/* Playback                                                            */
/* ------------------------------------------------------------------ */


#define AUDIO_PACKET_FRAMES (64 * 1024)
#define AUDIO_PUSH_FRAMES   2048

static int16_t aud_st[AUDIO_PUSH_FRAMES * 2];

/* Mix to stereo in small chunks and return the number of input frames queued. */
static int push_stereo(const int16_t *in, int ns, int ch)
{
    if (ns <= 0) return 0;
    if (ch == 2) return audio_out_push(in, ns);

    int total = 0;
    while (total < ns) {
        int n = ns - total;
        int space = audio_out_space();
        if (n > space) n = space;
        if (n > AUDIO_PUSH_FRAMES) n = AUDIO_PUSH_FRAMES;
        if (n <= 0) break;

        if (ch == 1) {
            for (int i = 0; i < n; i++)
                aud_st[i*2] = aud_st[i*2+1] = in[total + i];
        } else {
            for (int i = 0; i < n; i++) {
                aud_st[i*2]   = in[(total + i)*ch];
                aud_st[i*2+1] = in[(total + i)*ch + 1];
            }
        }
        int pushed = audio_out_push(aud_st, n);
        total += pushed;
        if (pushed < n) break;
    }
    return total;
}

static void play_file(const char *path)
{
    MoDemux mux;
    if (mo_demux_open(&mux, path) != 0) return;

    MobiDecoder *dec = mobi_open(mux.width, mux.height);
    if (!dec) { mo_demux_close(&mux); return; }

    int is_vorbis = mux.audio_type == MO_AUDIO_VORBIS;
    int has_audio = mux.audio_type != MO_AUDIO_NONE;
    int ach = mux.channels > 0 ? mux.channels : 2;

    /* Decode every codec incrementally into a bounded per-section buffer. */
    MoAudio audio_dec;
    MoVorbis *vorbis_dec = NULL;
    int16_t *audio_buf = NULL;
    int audio_pending = 0;
    int audio_pos = 0;

    if (has_audio) {
        if (is_vorbis) {
            vorbis_dec = mo_vorbis_open(&mux);
            if (vorbis_dec) ach = mo_vorbis_channels(vorbis_dec);
            else has_audio = 0;
        } else {
            mo_audio_init(&audio_dec, mux.audio_type, ach);
        }
        if (has_audio)
            audio_buf = malloc((size_t)AUDIO_PACKET_FRAMES * ach * sizeof(int16_t));
        if (!audio_buf) has_audio = 0;
    }
    if (has_audio) audio_out_start(mux.sample_rate);

    /* Frame rate is fps_den/fps_num (fps_num is fixed at 256), so the frame
     * period in seconds is fps_num/fps_den. e.g. fps_den=7680 -> 30 fps. */
    int period_us = mux.fps_den > 0
        ? (int)((long long)mux.fps_num * 1000000 / mux.fps_den)
        : 33333;
    int hz = vid_refresh_hz();
    int vspf = (int)(((long long)period_us * hz + 500000) / 1000000);
    if (vspf < 1) vspf = 1;



    vid_clear_both();                 /* letterbox bars are static; clear once */
    long long start_retrace = vid_retraces();
    MoPacket pkt;
    int stop = 0, paused = 0;

    while (!stop && mo_demux_read(&mux, &pkt) == 1) {
        if (audio_pending > 0) {
            int pushed = push_stereo(audio_buf + audio_pos * ach,
                                     audio_pending, ach);
            audio_pos += pushed;
            audio_pending -= pushed;
            if (audio_pending == 0) audio_pos = 0;
        }

        /* pause loop: hold on the current frame, keep polling input */
        while (paused && !stop) {
            u32 pb = input_down();
            long long ideal_retrace = (long long)mux.cur_frame * period_us * hz / 1000000;
            if (pb & (WPAD_BUTTON_A | WPAD_BUTTON_PLUS)) { paused = 0; start_retrace = vid_retraces() - ideal_retrace; }
            if (pb & (WPAD_BUTTON_B | WPAD_BUTTON_HOME)) stop = 1;
            VIDEO_WaitVSync();
        }
        if (stop) break;

        if (!pkt.is_audio) {
            long long ideal_retrace = (long long)mux.cur_frame * period_us * hz / 1000000;
            long target = start_retrace + ideal_retrace;
            MoFrame *fr = NULL;
            if (mobi_decode(dec, pkt.data, pkt.size, &fr) == 0 && fr) {
                int drop = (int)(vid_retraces() - target) > vspf;
                if (!drop) {
                    vid_draw_frame(fr);
                    vid_flip();
                }
            }
            /* Hold this frame until its scheduled retrace. Absolute schedule
             * self-corrects: a slow frame simply waits less (or not at all). */
            long long current_retrace = vid_retraces() - start_retrace;
            while (current_retrace < ideal_retrace) {
                vid_vsync();
                current_retrace = vid_retraces() - start_retrace;
            }
        } else if (has_audio && pkt.size > 0) {
            /*
             * Append this section behind any PCM the output ring has not yet
             * accepted. Compacting only moves bounded decoded PCM; compressed
             * input is still consumed directly from the demuxer.
             */
            if (audio_pos > 0 && audio_pending > 0) {
                memmove(audio_buf, audio_buf + audio_pos * ach,
                        (size_t)audio_pending * ach * sizeof(int16_t));
                audio_pos = 0;
            }
            int available = AUDIO_PACKET_FRAMES - audio_pending;
            if (available > 0) {
                int decoded = is_vorbis
                    ? mo_vorbis_decode(vorbis_dec, pkt.data, pkt.size,
                                       audio_buf + audio_pending * ach, available)
                    : mo_audio_decode(&audio_dec, pkt.data, pkt.size,
                                      audio_buf + audio_pending * ach, available);
                if (decoded > 0) audio_pending += decoded;
            }
            if (audio_pending > 0) {
                int pushed = push_stereo(audio_buf, audio_pending, ach);
                audio_pos = pushed;
                audio_pending -= pushed;
                if (audio_pending == 0) audio_pos = 0;
            }
        }

        u32 b = input_down();
        if (b & (WPAD_BUTTON_B | WPAD_BUTTON_HOME)) stop = 1;
        if (b & (WPAD_BUTTON_A | WPAD_BUTTON_PLUS)) paused = 1;
    }

    if (has_audio) audio_out_stop();
    mo_vorbis_close(vorbis_dec);
    free(audio_buf);
    mobi_close(dec);
    mo_demux_close(&mux);
}

/* Shared playback loop for packed-RGB24 sources (KWZ / PPM / THP). Audio is
 * already pre-decoded into src->audio by the opener; we stream it against the
 * hardware retrace clock exactly like play_file() does for the .mo path. */
static void play_rgb(RgbSource *src)
{
    int has_audio = src->audio && src->audio_samples > 0;
    int ach = src->channels > 0 ? src->channels : 1;
    long vtotal = has_audio ? src->audio_samples : 0, vpos = 0;
    int frame_size = src->w * src->h * 3;
    uint8_t *rgb = malloc(frame_size);
    if (!rgb) return;

    if (has_audio) audio_out_start(src->sample_rate);

    double fps = src->fps > 0.01 ? src->fps : 30.0;
    int period_us = (int)(1000000.0 / fps);
    int hz = vid_refresh_hz();
    int vspf = (int)(((long long)period_us * hz + 500000) / 1000000);
    if (vspf < 1) vspf = 1;

    vid_clear_both();
    long long start_retrace = vid_retraces();
    int stop = 0, paused = 0, frame = 0;

    while (!stop) {
        while (paused && !stop) {
            u32 pb = input_down();
            long long ideal = (long long)frame * period_us * hz / 1000000;
            if (pb & (WPAD_BUTTON_A | WPAD_BUTTON_PLUS)) { paused = 0; start_retrace = vid_retraces() - ideal; }
            if (pb & (WPAD_BUTTON_B | WPAD_BUTTON_HOME)) stop = 1;
            VIDEO_WaitVSync();
        }
        if (stop) break;

        long long ideal_retrace = (long long)frame * period_us * hz / 1000000;
        long target = start_retrace + ideal_retrace;

        if (src->get_frame(src, frame, rgb) == 0) {
            int drop = (int)(vid_retraces() - target) > vspf;
            if (!drop) {
                vid_draw_rgb24(rgb, src->w, src->h);
                vid_flip();
            }
        }

        if (has_audio && vpos < vtotal) {
            long long current_retrace = vid_retraces() - start_retrace;
            long ideal_pos = (long)(current_retrace * src->sample_rate / hz);
            long target_pos = ideal_pos + src->sample_rate / 4;
            if (target_pos > vtotal) target_pos = vtotal;
            long n = target_pos - vpos;
            if (n > 0) {
                int space = audio_out_space();
                if (n > space) n = space;
                if (n > 0) {
                    push_stereo(src->audio + vpos * ach, (int)n, ach);
                    vpos += n;
                }
            }
        }

        long long current_retrace = vid_retraces() - start_retrace;
        while (current_retrace < ideal_retrace) {
            vid_vsync();
            current_retrace = vid_retraces() - start_retrace;
        }

        u32 b = input_down();
        if (b & (WPAD_BUTTON_B | WPAD_BUTTON_HOME)) stop = 1;
        if (b & (WPAD_BUTTON_A | WPAD_BUTTON_PLUS)) paused = 1;
        frame++;

        if (frame >= src->frame_count) {
            if (!src->loops) break;         /* PPM/KWZ loop; others stop */
            frame = 0; vpos = 0;
            start_retrace = vid_retraces();  /* restart the clock for the loop */
        }
    }

    if (has_audio) audio_out_stop();
    src->close(src);
    free(rgb);
}

/* dispatch by extension */
static void play_any(const char *path)
{
    RgbSource src;
    if (has_ext(path, ".kwz")) {
        if (kwz_open(&src, path) == 0) play_rgb(&src);
    } else if (has_ext(path, ".ppm")) {
        if (ppm_open(&src, path) == 0) play_rgb(&src);
    } else if (has_ext(path, ".thp")) {
        if (thp_open(&src, path) == 0) play_rgb(&src);
    } else {
        play_file(path);
    }
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    vid_init();
    con_init();
    AUDIO_Init(NULL);
    AUDIO_SetDSPSampleRate(AI_SAMPLERATE_48KHZ);
    input_init();

    if (!fatInitDefault()) {
        con_clear(); con_goto(2, 2);
        con_printf("ERROR: could not mount SD/USB storage.\n");
        con_goto(4, 2);
        con_printf("Press HOME to exit.");
        while (1) {
            if (input_down() & WPAD_BUTTON_HOME) break;
            VIDEO_WaitVSync();
        }
        input_shutdown();
        return 0;
    }

    char path[1024];
    strcpy(path, "sd:/");
    scan_dir(path);

    int sel = 0, top = 0;
    draw_browser(path, sel, top);

    while (1) {
        u32 b = input_down();

        if (b & WPAD_BUTTON_HOME) break;

        if (b & WPAD_BUTTON_DOWN) {
            if (sel < n_entries - 1) sel++;
            if (sel >= top + 22) top++;
            draw_browser(path, sel, top);
        } else if (b & WPAD_BUTTON_UP) {
            if (sel > 0) sel--;
            if (sel < top) top--;
            draw_browser(path, sel, top);
        } else if (b & (WPAD_BUTTON_RIGHT | WPAD_BUTTON_PLUS)) {
            sel += 22; if (sel > n_entries - 1) sel = n_entries - 1;
            if (sel < 0) sel = 0;
            top = sel - 21; if (top < 0) top = 0;
            draw_browser(path, sel, top);
        } else if (b & (WPAD_BUTTON_LEFT | WPAD_BUTTON_MINUS)) {
            sel -= 22; if (sel < 0) sel = 0;
            if (sel < top) top = sel;
            draw_browser(path, sel, top);
        } else if (b & WPAD_BUTTON_A) {
            if (n_entries > 0) {
                Entry *e = &entries[sel];
                if (e->is_dir) {
                    if (path[strlen(path)-1] != '/') strcat(path, "/");
                    strcat(path, e->name);
                    scan_dir(path);
                    sel = top = 0;
                } else {
                    char full[1280];
                    snprintf(full, sizeof(full), "%s/%s", path, e->name);
                    
                    con_clear();
                    con_goto(12, 35);
                    con_printf("Loading...");
                    
                    play_any(full);
                    /* restore console view */
                    con_init();
                }
                draw_browser(path, sel, top);
            }
        } else if (b & WPAD_BUTTON_B) {
            char *slash = strrchr(path, '/');
            if (slash && slash != path && strcmp(path, "sd:/") != 0) {
                if (slash == path + strlen(path) - 1) { /* trailing slash */
                    *slash = 0; slash = strrchr(path, '/');
                }
                if (slash) slash[1] = 0;
                scan_dir(path);
                sel = top = 0;
                draw_browser(path, sel, top);
            }
        }
        VIDEO_WaitVSync();
    }

    input_shutdown();
    return 0;
}
