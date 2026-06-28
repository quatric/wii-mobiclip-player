/* Unified controller input: Wii Remotes (1-4), Classic Controllers and
 * GameCube pads, all mapped to WPAD_BUTTON_* values. Mapping follows
 * GlowWii's DetectInput (github.com/larsenv/GlowWii). */
#ifndef INPUT_H
#define INPUT_H

#include <gctypes.h>

void input_init(void);

/* Newly-pressed buttons this poll (edge), mapped to WPAD_BUTTON_* values. */
u32 input_down(void);
/* Currently-held buttons, mapped to WPAD_BUTTON_* values. */
u32 input_held(void);

#endif
