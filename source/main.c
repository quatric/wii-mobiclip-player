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

/* playable: .mo (Mobiclip) */
static int is_playable(const char *s)
{
    return has_ext(s, ".mo");
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
        if (!strcmp(e->d_name, ".")) continue;
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


static int16_t aud_st[64 * 1024 * 2];

/* mix `ns` interleaved `ch`-channel int16 samples down to stereo and queue */
static void push_stereo(const int16_t *in, int ns, int ch)
{
    if (ns <= 0) return;
    if (ch == 2) {
        audio_out_push(in, ns);
    } else if (ch == 1) {
        for (int i = 0; i < ns; i++) aud_st[i*2] = aud_st[i*2+1] = in[i];
        audio_out_push(aud_st, ns);
    } else {                       /* >2ch: take first two channels */
        for (int i = 0; i < ns; i++) {
            aud_st[i*2]   = in[i*ch];
            aud_st[i*2+1] = in[i*ch + 1];
        }
        audio_out_push(aud_st, ns);
    }
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

    /* Vorbis decode (Tremor) is too heavy to run inline every video frame, so
     * pre-decode the whole track once and stream it per-frame (like Flipnote
     * BGM). Cheap codecs (ADPCM/PCM/FastAudio) stay inline. */
    int16_t *vpcm = NULL; long vtotal = 0, vpos = 0;

    if (has_audio) {
        MoDemux pre;
        if (mo_demux_open(&pre, path) == 0) {
            MoVorbis *pv = NULL;
            MoAudio aud;
            if (is_vorbis) pv = mo_vorbis_open(&pre);
            else mo_audio_init(&aud, mux.audio_type, mux.channels);
            
            ach = is_vorbis ? mo_vorbis_channels(pv) : mux.channels;
            long cap = mux.frame_count > 0
                ? ((long)mux.frame_count * mux.fps_num / mux.fps_den + 2) * (long)mux.sample_rate
                : (long)mux.sample_rate * 600;
            vpcm = calloc(cap * ach, sizeof(int16_t));
            if (vpcm) {
                MoPacket pk;
                long running_pos = 0;
                int is_continuous = 0;
                while (mo_demux_read(&pre, &pk) == 1) {
                    if (pk.is_audio && pk.size > 0) {
                        unsigned seq = pk.size >= 2 ? (pk.data[0] | (pk.data[1] << 8)) : 0;
                        long pos = 0;
                        if (!is_continuous) {
                            running_pos = (long)((long long)pk.frame_index * pre.fps_num * pre.sample_rate / pre.fps_den);
                            if (seq != 0xFFFF && is_vorbis && pk.size >= 4) {
                                running_pos += (int16_t)(pk.data[2] | (pk.data[3] << 8));
                            }
                            is_continuous = 1;
                        }
                        pos = running_pos;
                        
                        if (pos < 0) pos = 0;
                        if (pos > cap - 8192) continue; /* drop if out of bounds */
                        
                        int decoded = 0;
                        if (is_vorbis) decoded = mo_vorbis_decode(pv, pk.data, pk.size, vpcm + pos * ach, (int)(cap - pos));
                        else decoded = mo_audio_decode(&aud, pk.data, pk.size, vpcm + pos * ach, (int)(cap - pos));
                        
                        running_pos += decoded;
                        if (pos + decoded > vtotal) vtotal = pos + decoded;
                    }
                }
            }
            if (is_vorbis) mo_vorbis_close(pv);
            mo_demux_close(&pre);
        }
        if (!vpcm) has_audio = 0;
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
            /* Stream audio based on the hardware clock to prevent starvation. */
            if (vpcm && vpos < vtotal) {
                /* Target 0.25 seconds ahead of current retrace to keep the ring buffer full */
                long long current_retrace = vid_retraces() - start_retrace;
                long ideal_pos = (long)(current_retrace * mux.sample_rate / hz);
                long target_pos = ideal_pos + mux.sample_rate / 4;
                if (target_pos > vtotal) target_pos = vtotal;
                
                long n = target_pos - vpos;
                if (n > 0) {
                    /* Only push what fits in the ring buffer without dropping */
                    int space = audio_out_space();
                    if (n > space) n = space;
                    
                    if (n > 0) {
                        push_stereo(vpcm + vpos * ach, (int)n, ach);
                        vpos += n;
                    }
                }
            }
            /* Hold this frame until its scheduled retrace. Absolute schedule
             * self-corrects: a slow frame simply waits less (or not at all). */
            long long current_retrace = vid_retraces() - start_retrace;
            while (current_retrace < ideal_retrace) {
                vid_vsync();
                current_retrace = vid_retraces() - start_retrace;
            }
        }

        u32 b = input_down();
        if (b & (WPAD_BUTTON_B | WPAD_BUTTON_HOME)) stop = 1;
        if (b & (WPAD_BUTTON_A | WPAD_BUTTON_PLUS)) paused = 1;
    }

    if (has_audio) audio_out_stop();
    free(vpcm);
    mobi_close(dec);
    mo_demux_close(&mux);
}

/* dispatch by extension */
static void play_any(const char *path)
{
    play_file(path);
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

    return 0;
}
