/* Unified input bridge. HID (and any future) drivers emit through these sinks,
 * which route keyboard text to the TTY line discipline, key events to the input
 * queue, and pointer events to the mouse queue — tagging each with its source so
 * userland can distinguish PS/2 from USB and keyboard from mouse. */
#ifndef MYOS_HID_INPUT_H
#define MYOS_HID_INPUT_H

#include "types.h"

/* Initialise the unified input stack. Emits "[OK] Unified input stack online". */
void unified_input_init(void);

void input_emit_key(uint16_t keycode, int down, uint16_t source);
void input_emit_text(char c, uint16_t source);
void input_emit_mouse_move(int dx, int dy, uint8_t buttons, uint16_t source);
void input_emit_mouse_button(uint8_t buttons, uint16_t source);
void input_emit_mouse_wheel(int delta, uint16_t source);

#endif /* MYOS_HID_INPUT_H */
