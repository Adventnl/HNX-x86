/* PS/2 keyboard: scancode-set-1 decode + shift state. */
#ifndef MYOS_PS2_KEYBOARD_H
#define MYOS_PS2_KEYBOARD_H

#include "types.h"

void ps2_keyboard_init(void);
void ps2_keyboard_handle_scancode(uint8_t scancode);

#endif /* MYOS_PS2_KEYBOARD_H */
