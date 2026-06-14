/* US QWERTY scancode-set-1 -> ASCII translation. */
#ifndef MYOS_KEYMAP_US_H
#define MYOS_KEYMAP_US_H

#include "types.h"

/* Translate a (press) scancode to ASCII, honoring shift. Returns 0 if the key
 * has no printable/edit character. '\n' for Enter, '\b' for Backspace. */
char keymap_us_translate(uint8_t scancode, int shift);

/* Scancode-set-1 modifiers. */
#define SC_LSHIFT      0x2A
#define SC_RSHIFT      0x36
#define SC_ENTER       0x1C
#define SC_BACKSPACE   0x0E
#define SC_RELEASE_BIT 0x80

#endif /* MYOS_KEYMAP_US_H */
