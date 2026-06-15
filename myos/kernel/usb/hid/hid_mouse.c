/* USB HID boot mouse (see hid_mouse.h). */
#include "hid_mouse.h"
#include "hid_input.h"
#include "input_event.h"
#include "log.h"

void hid_mouse_attach(struct usb_device *dev) {
    (void)dev;
    kernel_log_ok("USB mouse online");
}

void hid_mouse_handle_report(struct usb_device *dev, const uint8_t *report, int len) {
    (void)dev;
    if (!report || len < 3) {
        return;
    }
    uint8_t buttons = report[0];
    int dx = (int)(int8_t)report[1];
    int dy = (int)(int8_t)report[2];
    int wheel = (len >= 4) ? (int)(int8_t)report[3] : 0;

    input_emit_mouse_move(dx, dy, buttons, INPUT_SRC_USB_MOUSE);
    if (wheel) {
        input_emit_mouse_wheel(wheel, INPUT_SRC_USB_MOUSE);
    }
}
