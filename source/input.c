#include <wiiuse/wpad.h>
#include <ogc/pad.h>
#ifdef HAVE_WIIDRC
#include <wiidrc/wiidrc.h>
#endif
#ifdef HAVE_WUPC
#include <wupc/wupc.h>
#endif
#ifdef HAVE_SICKSAXIS
#include <ogc/usb.h>
#include <sicksaxis.h>
#endif
#include "input.h"

#ifdef HAVE_SICKSAXIS
static struct ss_device ds3;
static int ds3_initialized;
static u32 ds3_previous;
#endif

static u32 map_classic(u32 buttons)
{
    u32 out = 0;
    if (buttons & WPAD_CLASSIC_BUTTON_ZR) out |= WPAD_BUTTON_PLUS;
    if (buttons & WPAD_CLASSIC_BUTTON_ZL) out |= WPAD_BUTTON_MINUS;
    if (buttons & WPAD_CLASSIC_BUTTON_PLUS)  out |= WPAD_BUTTON_PLUS;
    if (buttons & WPAD_CLASSIC_BUTTON_MINUS) out |= WPAD_BUTTON_MINUS;
    if (buttons & WPAD_CLASSIC_BUTTON_A)     out |= WPAD_BUTTON_A;
    if (buttons & WPAD_CLASSIC_BUTTON_B)     out |= WPAD_BUTTON_B;
    if (buttons & WPAD_CLASSIC_BUTTON_X)     out |= WPAD_BUTTON_2;
    if (buttons & WPAD_CLASSIC_BUTTON_Y)     out |= WPAD_BUTTON_1;
    if (buttons & WPAD_CLASSIC_BUTTON_HOME)  out |= WPAD_BUTTON_HOME;
    if (buttons & WPAD_CLASSIC_BUTTON_UP)    out |= WPAD_BUTTON_UP;
    if (buttons & WPAD_CLASSIC_BUTTON_DOWN)  out |= WPAD_BUTTON_DOWN;
    if (buttons & WPAD_CLASSIC_BUTTON_LEFT)  out |= WPAD_BUTTON_LEFT;
    if (buttons & WPAD_CLASSIC_BUTTON_RIGHT) out |= WPAD_BUTTON_RIGHT;
    return out;
}

#ifdef HAVE_WIIDRC
static u32 map_drc(u32 buttons)
{
    u32 out = 0;
    if (buttons & (WIIDRC_BUTTON_R | WIIDRC_BUTTON_ZR)) out |= WPAD_BUTTON_PLUS;
    if (buttons & (WIIDRC_BUTTON_L | WIIDRC_BUTTON_ZL)) out |= WPAD_BUTTON_MINUS;
    if (buttons & WIIDRC_BUTTON_PLUS)  out |= WPAD_BUTTON_PLUS;
    if (buttons & WIIDRC_BUTTON_MINUS) out |= WPAD_BUTTON_MINUS;
    if (buttons & WIIDRC_BUTTON_A)     out |= WPAD_BUTTON_A;
    if (buttons & WIIDRC_BUTTON_B)     out |= WPAD_BUTTON_B;
    if (buttons & WIIDRC_BUTTON_X)     out |= WPAD_BUTTON_2;
    if (buttons & WIIDRC_BUTTON_Y)     out |= WPAD_BUTTON_1;
    if (buttons & WIIDRC_BUTTON_HOME)  out |= WPAD_BUTTON_HOME;
    if (buttons & WIIDRC_BUTTON_UP)    out |= WPAD_BUTTON_UP;
    if (buttons & WIIDRC_BUTTON_DOWN)  out |= WPAD_BUTTON_DOWN;
    if (buttons & WIIDRC_BUTTON_LEFT)  out |= WPAD_BUTTON_LEFT;
    if (buttons & WIIDRC_BUTTON_RIGHT) out |= WPAD_BUTTON_RIGHT;
    return out;
}
#endif

#ifdef HAVE_SICKSAXIS
static u32 map_ds3(void)
{
    const struct SS_BUTTONS *b = &ds3.pad.buttons;
    u32 out = 0;
    if (b->start || b->R1 || b->R2) out |= WPAD_BUTTON_PLUS;
    if (b->select || b->L1 || b->L2) out |= WPAD_BUTTON_MINUS;
    if (b->cross)    out |= WPAD_BUTTON_A;
    if (b->circle)   out |= WPAD_BUTTON_B;
    if (b->triangle) out |= WPAD_BUTTON_2;
    if (b->square)   out |= WPAD_BUTTON_1;
    if (b->PS)       out |= WPAD_BUTTON_HOME;
    if (b->up)       out |= WPAD_BUTTON_UP;
    if (b->down)     out |= WPAD_BUTTON_DOWN;
    if (b->left)     out |= WPAD_BUTTON_LEFT;
    if (b->right)    out |= WPAD_BUTTON_RIGHT;
    return out;
}

static u32 poll_ds3(int down)
{
    if (!ds3_initialized) return 0;
    if (!ss_is_connected(&ds3)) {
        ds3_previous = 0;
        if (ss_open(&ds3) > 0)
            ss_start_reading(&ds3);
        else
            return 0;
    }
    u32 held = map_ds3();
    u32 pressed = held & ~ds3_previous;
    ds3_previous = held;
    return down ? pressed : held;
}
#endif

void input_init(void)
{
#ifdef HAVE_WUPC
    WUPC_Init();
#endif
    WPAD_Init();
    PAD_Init();
#ifdef HAVE_WIIDRC
    WiiDRC_Init();
#endif
#ifdef HAVE_SICKSAXIS
    USB_Initialize();
    if (ss_init() >= 0 && ss_initialize(&ds3) >= 0) {
        ds3_initialized = 1;
        if (ss_open(&ds3) > 0)
            ss_start_reading(&ds3);
    }
#endif
}

void input_shutdown(void)
{
#ifdef HAVE_SICKSAXIS
    if (ds3_initialized) {
        if (ss_is_connected(&ds3)) ss_stop_reading(&ds3);
        ss_close(&ds3);
        ds3_initialized = 0;
    }
#endif
#ifdef HAVE_WUPC
    WUPC_Shutdown();
#endif
}

static u32 detect(int down)
{
    u32 pressed = 0, buttons = 0, gc = 0;
#ifdef HAVE_WUPC
    WUPC_UpdateButtonStats();
    for (int chan = 0; chan < 4; chan++)
        buttons |= down ? WUPC_ButtonsDown(chan) : WUPC_ButtonsHeld(chan);
    pressed |= map_classic(buttons);
#endif
    buttons = 0;
    if (WPAD_ScanPads() > WPAD_ERR_NONE) {
        for (int chan = 0; chan < 4; chan++)
            buttons |= down ? WPAD_ButtonsDown(chan) : WPAD_ButtonsHeld(chan);
        pressed |= buttons | map_classic(buttons);
    }
#ifdef HAVE_WIIDRC
    if (WiiDRC_Inited() && WiiDRC_Connected() && WiiDRC_ScanPads()) {
        buttons = down ? WiiDRC_ButtonsDown() : WiiDRC_ButtonsHeld();
        pressed |= map_drc(buttons);
    }
#endif
#ifdef HAVE_SICKSAXIS
    pressed |= poll_ds3(down);
#endif
    if (PAD_ScanPads() > PAD_ERR_NONE) {
        for (int chan = 0; chan < 4; chan++)
            gc |= down ? PAD_ButtonsDown(chan) : PAD_ButtonsHeld(chan);
        if (gc & PAD_TRIGGER_R)    pressed |= WPAD_BUTTON_PLUS;
        if (gc & PAD_TRIGGER_L)    pressed |= WPAD_BUTTON_MINUS;
        if (gc & PAD_BUTTON_A)     pressed |= WPAD_BUTTON_A;
        if (gc & PAD_BUTTON_B)     pressed |= WPAD_BUTTON_B;
        if (gc & PAD_BUTTON_X)     pressed |= WPAD_BUTTON_2;
        if (gc & PAD_BUTTON_Y)     pressed |= WPAD_BUTTON_1;
        if (gc & PAD_BUTTON_MENU)  pressed |= WPAD_BUTTON_HOME;
        if (gc & PAD_BUTTON_UP)    pressed |= WPAD_BUTTON_UP;
        if (gc & PAD_BUTTON_DOWN)  pressed |= WPAD_BUTTON_DOWN;
        if (gc & PAD_BUTTON_LEFT)  pressed |= WPAD_BUTTON_LEFT;
        if (gc & PAD_BUTTON_RIGHT) pressed |= WPAD_BUTTON_RIGHT;
    }
    return pressed;
}

u32 input_down(void) { return detect(1); }
u32 input_held(void) { return detect(0); }
