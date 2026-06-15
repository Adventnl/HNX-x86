/* USB HID boot-protocol mouse: turns boot reports into unified pointer events. */
#ifndef MYOS_HID_MOUSE_H
#define MYOS_HID_MOUSE_H

#include "types.h"

struct usb_device;

void hid_mouse_attach(struct usb_device *dev);   /* logs "[OK] USB mouse online" */

/* Process one boot-mouse report: [buttons][dx][dy]([wheel]). */
void hid_mouse_handle_report(struct usb_device *dev, const uint8_t *report, int len);

#endif /* MYOS_HID_MOUSE_H */
