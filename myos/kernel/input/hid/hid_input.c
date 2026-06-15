/* Unified input bridge (see hid_input.h). */
#include "hid_input.h"
#include "input_event.h"
#include "input_queue.h"
#include "mouse.h"
#include "hw_event_bus.h"
#include "tty.h"
#include "log.h"

void unified_input_init(void) {
    mouse_init();
    kernel_log_ok("Unified input stack online");
}

void input_emit_key(uint16_t keycode, int down, uint16_t source) {
    struct input_event ev;
    ev.type = down ? INPUT_EVENT_KEY_DOWN : INPUT_EVENT_KEY_UP;
    ev.code = keycode;
    ev.value = down ? 1 : 0;
    ev.value2 = 0;
    ev.source = source;
    ev._pad = 0;
    input_queue_push(&ev);
    hw_event_emit(HW_EVENT_INPUT, source, keycode, "key");
}

void input_emit_text(char c, uint16_t source) {
    struct input_event ev;
    ev.type = INPUT_EVENT_TEXT;
    ev.code = (uint16_t)(uint8_t)c;
    ev.value = (uint8_t)c;
    ev.value2 = 0;
    ev.source = source;
    ev._pad = 0;
    input_queue_push(&ev);
    /* Feed the canonical line discipline so the shell/TTY sees the character,
     * exactly as the PS/2 path does. */
    tty_input_char(c);
}

void input_emit_mouse_move(int dx, int dy, uint8_t buttons, uint16_t source) {
    mouse_process(dx, dy, buttons, 0, source);
}

void input_emit_mouse_button(uint8_t buttons, uint16_t source) {
    mouse_process(0, 0, buttons, 0, source);
}

void input_emit_mouse_wheel(int delta, uint16_t source) {
    mouse_process(0, 0, 0, delta, source);
}
