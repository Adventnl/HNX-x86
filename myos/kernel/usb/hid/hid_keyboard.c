/* USB HID boot keyboard (see hid_keyboard.h). */
#include "hid_keyboard.h"
#include "hid_usage.h"
#include "hid_input.h"
#include "input_event.h"
#include "string.h"
#include "log.h"

/* Previous boot report (QEMU presents a single keyboard). */
static uint8_t g_prev[8];

void hid_keyboard_attach(struct usb_device *dev) {
    (void)dev;
    memset(g_prev, 0, sizeof(g_prev));
    kernel_log_ok("USB keyboard online");
}

static int contains_key(const uint8_t *report, uint8_t key) {
    for (int i = 2; i < 8; i++) {
        if (report[i] == key) {
            return 1;
        }
    }
    return 0;
}

void hid_keyboard_handle_report(struct usb_device *dev, const uint8_t *report, int len) {
    (void)dev;
    if (!report || len < 8) {
        return;
    }
    uint8_t mods = report[0];
    int shift = (mods & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)) ? 1 : 0;

    /* Releases: keys present last time but not now. */
    for (int i = 2; i < 8; i++) {
        uint8_t k = g_prev[i];
        if (k >= 4 && !contains_key(report, k)) {
            input_emit_key(hid_usage_to_keycode(k), 0, INPUT_SRC_USB_KEYBOARD);
        }
    }
    /* Presses: keys present now but not last time. */
    for (int i = 2; i < 8; i++) {
        uint8_t k = report[i];
        if (k >= 4 && !contains_key(g_prev, k)) {
            input_emit_key(hid_usage_to_keycode(k), 1, INPUT_SRC_USB_KEYBOARD);
            char c = hid_usage_to_char(k, shift);
            if (c) {
                input_emit_text(c, INPUT_SRC_USB_KEYBOARD);
            }
        }
    }
    memcpy(g_prev, report, 8);
}
