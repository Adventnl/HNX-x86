/* USB HID self-tests. A known boot keyboard / mouse report is driven through the
 * real hid_keyboard/hid_mouse handlers (the same functions the interrupt path
 * calls) and the resulting unified events are asserted — exactly analogous to
 * the PS/2 scancode-injection test. */
#include "hid_tests.h"
#include "hid_keyboard.h"
#include "hid_mouse.h"
#include "input_event.h"
#include "input_queue.h"
#include "mouse_event.h"
#include "tty.h"
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

static int test_hid_keyboard(void) {
    drain_input();
    /* Press 'a' (usage 0x04) with no modifiers. */
    uint8_t press[8] = {0, 0, 0x04, 0, 0, 0, 0, 0};
    hid_keyboard_handle_report(0, press, 8);

    int got_key = 0, got_text = 0;
    struct input_event ev;
    while (input_queue_pop(&ev) == 0) {
        if (ev.source == INPUT_SRC_USB_KEYBOARD &&
            ev.type == INPUT_EVENT_KEY_DOWN && ev.code == 0x04) {
            got_key = 1;
        }
        if (ev.source == INPUT_SRC_USB_KEYBOARD &&
            ev.type == INPUT_EVENT_TEXT && ev.code == 'a') {
            got_text = 1;
        }
    }
    /* Release to leave the boot-report state clean. */
    uint8_t release[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    hid_keyboard_handle_report(0, release, 8);
    drain_input();
    tty_reset_input();
    return (got_key && got_text) ? 0 : -1;
}

static int test_hid_mouse(void) {
    drain_mouse();
    /* Left button held, move +5/-5, no wheel. */
    uint8_t report[4] = {0x01, 5, (uint8_t)(int8_t)-5, 0};
    hid_mouse_handle_report(0, report, 4);

    int got_move = 0;
    struct mouse_event me;
    while (mouse_event_pop(&me) == 0) {
        if (me.source == INPUT_SRC_USB_MOUSE && me.dx == 5 && me.dy == -5) {
            got_move = 1;
        }
    }
    return got_move ? 0 : -1;
}

void hid_tests_run(void) {
    if (test_hid_keyboard() == 0) {
        kernel_log_line("[PASS] hid keyboard test");
    } else {
        kernel_log_error("hid keyboard test failed");
    }
    if (test_hid_mouse() == 0) {
        kernel_log_line("[PASS] hid mouse test");
    } else {
        kernel_log_error("hid mouse test failed");
    }
}
