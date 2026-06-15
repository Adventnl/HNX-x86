/* USB HID boot-protocol keyboard: turns 8-byte boot reports into unified key +
 * text events. */
#ifndef MYOS_HID_KEYBOARD_H
#define MYOS_HID_KEYBOARD_H

#include "types.h"

struct usb_device;

void hid_keyboard_attach(struct usb_device *dev);   /* logs "[OK] USB keyboard online" */

/* Process one boot-keyboard report (modifiers, reserved, up to 6 keycodes). */
void hid_keyboard_handle_report(struct usb_device *dev, const uint8_t *report, int len);

#endif /* MYOS_HID_KEYBOARD_H */
