/* HID usage tables: translate boot-keyboard usage IDs (Usage Page 0x07) to
 * ASCII, honoring the shift modifier. */
#ifndef MYOS_HID_USAGE_H
#define MYOS_HID_USAGE_H

#include "types.h"

/* HID keyboard modifier bits (boot report byte 0). */
#define HID_MOD_LCTRL   0x01
#define HID_MOD_LSHIFT  0x02
#define HID_MOD_LALT    0x04
#define HID_MOD_LGUI    0x08
#define HID_MOD_RCTRL   0x10
#define HID_MOD_RSHIFT  0x20

/* Translate a keyboard usage to ASCII (0 if no printable/edit char). Returns
 * '\n' for Enter, '\b' for Backspace, '\t' for Tab. */
char hid_usage_to_char(uint8_t usage, int shift);

/* A stable keycode for the usage (the usage id itself). */
uint16_t hid_usage_to_keycode(uint8_t usage);

#endif /* MYOS_HID_USAGE_H */
