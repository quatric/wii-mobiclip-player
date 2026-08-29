#include <wiiuse/wpad.h>
#include <ogc/pad.h>
#include <ogc/usb.h>
#include <ogc/lwp.h>
#include <wiikeyboard/usbkeyboard.h>
#include <unistd.h>
#ifdef HAVE_WIIDRC
#include <wiidrc/wiidrc.h>
#endif
#ifdef HAVE_WUPC
#include <wupc/wupc.h>
#endif
#ifdef HAVE_SICKSAXIS
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

/* USB keyboard: a dedicated LWP thread owns USB_Initialize()/
 * USBKeyboard_Scan(), and its event callback maintains two bitmasks --
 * kbd_held (current physical key state, WPAD_BUTTON_* mapped) and
 * kbd_down_accum (set only on the press transition, consumed/cleared by
 * input_down()) -- so both input_down() and input_held() get correct
 * edge/level semantics from the keyboard, same as they already get from
 * WPAD/PAD. */
static lwp_t kbd_thread_hndl = LWP_THREAD_NULL;
static volatile bool kbd_thread_should_run = false;
static u32 kbd_held;
static u32 kbd_down_accum;

static u32 hid_keycode_to_wpad(u8 keyCode)
{
    switch (keyCode)
    {
        case 0x52: return WPAD_BUTTON_UP;
        case 0x51: return WPAD_BUTTON_DOWN;
        case 0x50: return WPAD_BUTTON_LEFT;
        case 0x4F: return WPAD_BUTTON_RIGHT;
        case 0x28: /* Enter */
        case 0x58: /* Enter (numpad) */
        case 0x2C: /* Space -- play/pause */
            return WPAD_BUTTON_A;
        case 0x2A: /* Backspace -- back/stop */
            return WPAD_BUTTON_B;
        case 0x29: /* Escape -- quit */
        case 0x4A: /* Home */
            return WPAD_BUTTON_HOME;
        case 0x1B: return WPAD_BUTTON_2; /* X */
        case 0x1C: return WPAD_BUTTON_1; /* Y */
        case 0x4C: return WPAD_BUTTON_MINUS; /* Delete */
        default: return 0;
    }
}

static void kbd_event_handler(USBKeyboard_event event)
{
    if (event.type != USBKEYBOARD_PRESSED && event.type != USBKEYBOARD_RELEASED)
        return;

    u32 button = hid_keycode_to_wpad(event.keyCode);
    if (!button)
        return;

    if (event.type == USBKEYBOARD_PRESSED)
    {
        if (!(kbd_held & button))
            kbd_down_accum |= button; /* mark "down" only on the transition */
        kbd_held |= button;
    }
    else
    {
        kbd_held &= ~button;
    }
}

static void *kbd_thread(void *userp)
{
    (void)userp;
    while (kbd_thread_should_run)
    {
        if (!USBKeyboard_IsConnected())
            USBKeyboard_Open(kbd_event_handler);

        USBKeyboard_Scan();
        usleep(400);
    }
    return NULL;
}

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
    USB_Initialize();
#ifdef HAVE_SICKSAXIS
    if (ss_init() >= 0 && ss_initialize(&ds3) >= 0) {
        ds3_initialized = 1;
        if (ss_open(&ds3) > 0)
            ss_start_reading(&ds3);
    }
#endif

    USBKeyboard_Initialize();
    kbd_thread_should_run = true;
    LWP_CreateThread(&kbd_thread_hndl, kbd_thread, NULL, NULL, 0x4000, 0x7F);
}

void input_shutdown(void)
{
    kbd_thread_should_run = false;
    usleep(400);
    USBKeyboard_Close();
    USBKeyboard_Deinitialize();
    if (kbd_thread_hndl != LWP_THREAD_NULL)
        LWP_JoinThread(kbd_thread_hndl, NULL);
    kbd_thread_hndl = LWP_THREAD_NULL;

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

    /* USB keyboard, same edge/held split as WPAD/PAD/GC above. Merged in
     * unconditionally (not as an early-return fallback) since origin/main's
     * detect() already checks every backend unconditionally rather than
     * short-circuiting. */
    if (down)
    {
        pressed |= kbd_down_accum;
        kbd_down_accum = 0;
    }
    else
    {
        pressed |= kbd_held;
    }

    return pressed;
}

u32 input_down(void) { return detect(1); }
u32 input_held(void) { return detect(0); }
