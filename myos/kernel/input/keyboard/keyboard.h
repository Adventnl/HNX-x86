/* Keyboard layer: turns decoded characters into TTY input + input events. */
#ifndef MYOS_KEYBOARD_H
#define MYOS_KEYBOARD_H

#include "types.h"

void keyboard_init(void);            /* logs "[OK] Keyboard input online" */

/* Called by the PS/2 layer with a decoded printable/edit character. */
void keyboard_emit_char(char c);

/* Inject a raw scancode (verification / scripted input). */
void keyboard_inject_scancode(uint8_t scancode);

#endif /* MYOS_KEYBOARD_H */
