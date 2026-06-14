/* Keyboard scancode-injection + TTY canonical-input self-tests. Both leave the
 * cooked TTY buffer reset so the shell scripts that follow are clean. */
#include "input_tests.h"
#include "keyboard.h"
#include "keymap_us.h"
#include "input_queue.h"
#include "input_event.h"
#include "tty.h"
#include "string.h"
#include "log.h"

static void test_keyboard(void) {
    struct input_event ev;
    while (input_queue_pop(&ev) == 0) {     /* drain */
    }
    /* Inject 'a','b','c' (set-1 scancodes) and confirm they decode + queue. */
    keyboard_inject_scancode(0x1E);   /* a */
    keyboard_inject_scancode(0x30);   /* b */
    keyboard_inject_scancode(0x2E);   /* c */

    char got[8];
    int n = 0;
    while (n < 8 && input_queue_pop(&ev) == 0) {
        if (ev.value == 1) {
            got[n++] = keymap_us_translate((uint8_t)ev.code, 0);
        }
    }
    if (n >= 3 && got[0] == 'a' && got[1] == 'b' && got[2] == 'c') {
        kernel_log_line("[PASS] keyboard scripted injection");
    } else {
        kernel_log_error("keyboard scripted injection failed");
    }
}

static void test_tty_canonical(void) {
    tty_reset_input();
    /* Type "hix", backspace the 'x', press Enter -> cooked line "hi\n". */
    tty_input_char('h');
    tty_input_char('i');
    tty_input_char('x');
    tty_input_char('\b');
    tty_input_char('\n');

    char buf[16];
    memset(buf, 0, sizeof(buf));
    int64_t r = tty_read(buf, sizeof(buf));
    if (r == 3 && buf[0] == 'h' && buf[1] == 'i' && buf[2] == '\n') {
        kernel_log_line("[PASS] tty canonical input");
    } else {
        kernel_log_error("tty canonical input failed");
    }
    tty_reset_input();
}

void input_tests_run(void) {
    test_keyboard();
    test_tty_canonical();
}
