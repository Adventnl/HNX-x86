/* Unified input stack compatibility tests. Confirms PS/2 and USB keyboards both
 * reach the input queue + TTY through the unified path, USB mouse events are
 * queued, and TTY accepts unified keyboard text. Leaves the cooked TTY buffer
 * reset so the shell scripts that follow stay clean. */
#include "input_compat_tests.h"
#include "keyboard.h"
#include "keymap_us.h"
#include "hid_keyboard.h"
#include "hid_mouse.h"
#include "input_event.h"
#include "input_queue.h"
#include "mouse_event.h"
#include "tty.h"
#include "string.h"
#include "log.h"

static void drain_input(void) {
    struct input_event ev;
    while (input_queue_pop(&ev) == 0) {
    }
}

static void drain_mouse(void) {
    struct mouse_event me;
    while (mouse_event_pop(&me) == 0) {
    }
}

static int test_ps2_keyboard(void) {
    drain_input();
    keyboard_inject_scancode(0x1E);   /* a */
    keyboard_inject_scancode(0x30);   /* b */
    keyboard_inject_scancode(0x2E);   /* c */

    char got[8];
    int n = 0, src_ok = 1;
    struct input_event ev;
    while (n < 8 && input_queue_pop(&ev) == 0) {
        if (ev.value == 1) {
            if (ev.source != INPUT_SRC_PS2_KEYBOARD) {
                src_ok = 0;
            }
            got[n++] = keymap_us_translate((uint8_t)ev.code, 0);
        }
    }
    tty_reset_input();
    return (n >= 3 && src_ok && got[0] == 'a' && got[1] == 'b' && got[2] == 'c') ? 0 : -1;
}

static int test_usb_keyboard(void) {
    drain_input();
    uint8_t press[8] = {0, 0, 0x0E, 0, 0, 0, 0, 0};   /* 'k' */
    hid_keyboard_handle_report(0, press, 8);

    int got_key = 0, got_text = 0;
    struct input_event ev;
    while (input_queue_pop(&ev) == 0) {
        if (ev.source == INPUT_SRC_USB_KEYBOARD && ev.type == INPUT_EVENT_KEY_DOWN &&
            ev.code == 0x0E) {
            got_key = 1;
        }
        if (ev.source == INPUT_SRC_USB_KEYBOARD && ev.type == INPUT_EVENT_TEXT &&
            ev.code == 'k') {
            got_text = 1;
        }
    }
    uint8_t release[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    hid_keyboard_handle_report(0, release, 8);
    drain_input();
    tty_reset_input();
    return (got_key && got_text) ? 0 : -1;
}

static int test_usb_mouse(void) {
    drain_mouse();
    uint8_t report[4] = {0x02, (uint8_t)(int8_t)-3, 7, 0};  /* right button, -3/+7 */
    hid_mouse_handle_report(0, report, 4);

    int ok = 0;
    struct mouse_event me;
    while (mouse_event_pop(&me) == 0) {
        if (me.source == INPUT_SRC_USB_MOUSE && me.dx == -3 && me.dy == 7) {
            ok = 1;
        }
    }
    return ok ? 0 : -1;
}

static int test_tty_unified(void) {
    tty_reset_input();
    drain_input();
    /* Type "hi\n" via USB boot reports; each report is the full key state. */
    uint8_t r_h[8]   = {0, 0, 0x0B, 0, 0, 0, 0, 0};   /* 'h' */
    uint8_t r_i[8]   = {0, 0, 0x0C, 0, 0, 0, 0, 0};   /* 'i' */
    uint8_t r_ent[8] = {0, 0, 0x28, 0, 0, 0, 0, 0};   /* Enter */
    uint8_t r_up[8]  = {0, 0, 0, 0, 0, 0, 0, 0};
    hid_keyboard_handle_report(0, r_h, 8);
    hid_keyboard_handle_report(0, r_i, 8);
    hid_keyboard_handle_report(0, r_ent, 8);
    hid_keyboard_handle_report(0, r_up, 8);

    char buf[16];
    memset(buf, 0, sizeof(buf));
    int64_t r = tty_read(buf, sizeof(buf));
    tty_reset_input();
    drain_input();
    return (r == 3 && buf[0] == 'h' && buf[1] == 'i' && buf[2] == '\n') ? 0 : -1;
}

void input_compat_tests_run(void) {
    if (test_ps2_keyboard() == 0) {
        kernel_log_line("[PASS] ps2 keyboard still works");
    } else {
        kernel_log_error("ps2 keyboard compat test failed");
    }
    if (test_usb_keyboard() == 0) {
        kernel_log_line("[PASS] usb keyboard works");
    } else {
        kernel_log_error("usb keyboard compat test failed");
    }
    if (test_usb_mouse() == 0) {
        kernel_log_line("[PASS] usb mouse works");
    } else {
        kernel_log_error("usb mouse compat test failed");
    }
    if (test_tty_unified() == 0) {
        kernel_log_line("[PASS] tty accepts unified keyboard input");
    } else {
        kernel_log_error("tty unified input test failed");
    }
    tty_reset_input();
}
