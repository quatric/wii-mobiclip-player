#include <wiiuse/wpad.h>
#include <input_wii.h>

#include "input.h"

/* Controllers, keyboard and the optional Wii U / DualShock 3 backends are
 * handled by libwiiinput; everything arrives as WPAD_BUTTON_* bits. */

void input_init(void)
{
    /* Buttons only: the player needs no IR pointer and no mouse. */
    INPUT_InitEx(INPUT_INIT_NO_POINTER | INPUT_INIT_NO_MOUSE);

    INPUT_KeyboardMapSet(0x2C, WPAD_BUTTON_A); /* Space: play/pause */
    INPUT_KeyboardMapSet(0x1B, WPAD_BUTTON_2); /* X */
    INPUT_KeyboardMapSet(0x1C, WPAD_BUTTON_1); /* Y */
}

void input_shutdown(void)
{
    INPUT_Shutdown();
}

u32 input_down(void)
{
    INPUT_Update();
    return INPUT_ButtonsDown();
}

u32 input_held(void)
{
    INPUT_Update();
    return INPUT_ButtonsHeld();
}
