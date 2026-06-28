#include <wiiuse/wpad.h>
#include <ogc/pad.h>
#include "input.h"

void input_init(void)
{
    WPAD_Init();
    PAD_Init();
}

/* down=1 -> ButtonsDown (edge), down=0 -> ButtonsHeld */
static u32 detect(int down)
{
    u32 pressed = 0, gc = 0;

    /* Wii Remotes + Classic Controllers (ports 0..3) take precedence. */
    if (WPAD_ScanPads() > WPAD_ERR_NONE) {
        if (down)
            pressed = WPAD_ButtonsDown(0) | WPAD_ButtonsDown(1)
                    | WPAD_ButtonsDown(2) | WPAD_ButtonsDown(3);
        else
            pressed = WPAD_ButtonsHeld(0) | WPAD_ButtonsHeld(1)
                    | WPAD_ButtonsHeld(2) | WPAD_ButtonsHeld(3);

        if (pressed & WPAD_CLASSIC_BUTTON_ZR)    pressed |= WPAD_BUTTON_PLUS;
        if (pressed & WPAD_CLASSIC_BUTTON_ZL)    pressed |= WPAD_BUTTON_MINUS;
        if (pressed & WPAD_CLASSIC_BUTTON_PLUS)  pressed |= WPAD_BUTTON_PLUS;
        if (pressed & WPAD_CLASSIC_BUTTON_MINUS) pressed |= WPAD_BUTTON_MINUS;
        if (pressed & WPAD_CLASSIC_BUTTON_A)     pressed |= WPAD_BUTTON_A;
        if (pressed & WPAD_CLASSIC_BUTTON_B)     pressed |= WPAD_BUTTON_B;
        if (pressed & WPAD_CLASSIC_BUTTON_X)     pressed |= WPAD_BUTTON_2;
        if (pressed & WPAD_CLASSIC_BUTTON_Y)     pressed |= WPAD_BUTTON_1;
        if (pressed & WPAD_CLASSIC_BUTTON_HOME)  pressed |= WPAD_BUTTON_HOME;
        if (pressed & WPAD_CLASSIC_BUTTON_UP)    pressed |= WPAD_BUTTON_UP;
        if (pressed & WPAD_CLASSIC_BUTTON_DOWN)  pressed |= WPAD_BUTTON_DOWN;
        if (pressed & WPAD_CLASSIC_BUTTON_LEFT)  pressed |= WPAD_BUTTON_LEFT;
        if (pressed & WPAD_CLASSIC_BUTTON_RIGHT) pressed |= WPAD_BUTTON_RIGHT;
    }

    if (pressed) return pressed;

    /* Fall back to GameCube controllers (ports 0..3). */
    if (PAD_ScanPads() > PAD_ERR_NONE) {
        if (down)
            gc = PAD_ButtonsDown(0) | PAD_ButtonsDown(1)
               | PAD_ButtonsDown(2) | PAD_ButtonsDown(3);
        else
            gc = PAD_ButtonsHeld(0) | PAD_ButtonsHeld(1)
               | PAD_ButtonsHeld(2) | PAD_ButtonsHeld(3);

        if (gc & PAD_TRIGGER_R)   pressed |= WPAD_BUTTON_PLUS;
        if (gc & PAD_TRIGGER_L)   pressed |= WPAD_BUTTON_MINUS;
        if (gc & PAD_BUTTON_A)    pressed |= WPAD_BUTTON_A;
        if (gc & PAD_BUTTON_B)    pressed |= WPAD_BUTTON_B;
        if (gc & PAD_BUTTON_X)    pressed |= WPAD_BUTTON_2;
        if (gc & PAD_BUTTON_Y)    pressed |= WPAD_BUTTON_1;
        if (gc & PAD_BUTTON_MENU) pressed |= WPAD_BUTTON_HOME;
        if (gc & PAD_BUTTON_UP)   pressed |= WPAD_BUTTON_UP;
        if (gc & PAD_BUTTON_DOWN) pressed |= WPAD_BUTTON_DOWN;
        if (gc & PAD_BUTTON_LEFT) pressed |= WPAD_BUTTON_LEFT;
        if (gc & PAD_BUTTON_RIGHT) pressed |= WPAD_BUTTON_RIGHT;
    }
    return pressed;
}

u32 input_down(void) { return detect(1); }
u32 input_held(void) { return detect(0); }
