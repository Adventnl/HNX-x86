/* PS/2 keyboard scancode decode (set 1): tracks shift, emits input events +
 * decoded characters. */
#include "ps2_keyboard.h"
#include "keymap_us.h"
#include "keyboard.h"
#include "input_queue.h"
#include "input_event.h"

static int g_shift;

void ps2_keyboard_init(void) {
    g_shift = 0;
}

void ps2_keyboard_handle_scancode(uint8_t sc) {
    if (sc == SC_LSHIFT || sc == SC_RSHIFT) {
        g_shift = 1;
        return;
    }
    if (sc == (SC_LSHIFT | SC_RELEASE_BIT) || sc == (SC_RSHIFT | SC_RELEASE_BIT)) {
        g_shift = 0;
        return;
    }
    if (sc & SC_RELEASE_BIT) {
        struct input_event ev;
        input_event_init_key(&ev, (uint16_t)(sc & 0x7F), 0);
        input_queue_push(&ev);
        return;   /* key release: no character */
    }

    struct input_event ev;
    input_event_init_key(&ev, sc, 1);
    input_queue_push(&ev);

    char c = keymap_us_translate(sc, g_shift);
    if (c) {
        keyboard_emit_char(c);
    }
}
