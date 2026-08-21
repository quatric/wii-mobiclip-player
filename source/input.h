/* Unified controller input mapped to WPAD_BUTTON_* values.
 * Wii U GamePad, Wii U Pro Controller and DualShock 3 support are optional
 * build-time backends; Wii Remotes, Classic and GameCube pads remain standard. */
#ifndef INPUT_H
#define INPUT_H

#include <gctypes.h>

void input_init(void);
void input_shutdown(void);

/* Newly-pressed buttons this poll (edge), mapped to WPAD_BUTTON_* values. */
u32 input_down(void);
/* Currently-held buttons, mapped to WPAD_BUTTON_* values. */
u32 input_held(void);

#endif
