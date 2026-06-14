/* Keyboard layer. */
#include "keyboard.h"
#include "ps2_keyboard.h"
#include "tty.h"
#include "log.h"

void keyboard_init(void) {
    kernel_log_ok("Keyboard input online");
}

void keyboard_emit_char(char c) {
    tty_input_char(c);   /* feed the canonical line discipline */
}

void keyboard_inject_scancode(uint8_t scancode) {
    ps2_keyboard_handle_scancode(scancode);
}
