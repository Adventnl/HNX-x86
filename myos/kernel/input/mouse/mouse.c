/* Mouse device model (see mouse.h). */
#include "mouse.h"
#include "mouse_event.h"
#include "string.h"

static struct mouse_state g_state;
static uint8_t g_prev_buttons;

void mouse_init(void) {
    memset(&g_state, 0, sizeof(g_state));
    g_prev_buttons = 0;
    mouse_event_init();
}

void mouse_process(int dx, int dy, uint8_t buttons, int wheel, uint16_t source) {
    g_state.x += dx;
    g_state.y += dy;
    g_state.wheel += wheel;
    g_state.buttons = buttons;

    /* Movement event. */
    if (dx != 0 || dy != 0) {
        struct mouse_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.dx = (int16_t)dx;
        ev.dy = (int16_t)dy;
        ev.buttons = buttons;
        ev.source = source;
        mouse_event_push(&ev);
    }
    /* Button change events. */
    if (buttons != g_prev_buttons) {
        struct mouse_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.buttons = buttons;
        ev.source = source;
        mouse_event_push(&ev);
        g_prev_buttons = buttons;
    }
    /* Wheel event. */
    if (wheel != 0) {
        struct mouse_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.wheel = (int8_t)wheel;
        ev.buttons = buttons;
        ev.source = source;
        mouse_event_push(&ev);
    }
}

void mouse_get_state(struct mouse_state *out) {
    if (out) {
        *out = g_state;
    }
}
