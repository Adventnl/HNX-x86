/* Generic input event, unified across PS/2 and USB-HID sources.
 *
 * INPUT_EV_KEY is retained for the legacy PS/2 path (code = scancode, value =
 * press). Prompt 6 adds the unified event types and a `source` tag so userland
 * can tell PS/2 from USB and keyboard from mouse. */
#ifndef MYOS_INPUT_EVENT_H
#define MYOS_INPUT_EVENT_H

#include "types.h"

enum input_event_type {
    INPUT_EV_KEY = 1,            /* legacy: code = scancode, value = press   */
    INPUT_EVENT_KEY_DOWN,        /* code = keycode                           */
    INPUT_EVENT_KEY_UP,
    INPUT_EVENT_TEXT,            /* code = ASCII char                        */
    INPUT_EVENT_MOUSE_MOVE,      /* value = dx, value2 = dy                  */
    INPUT_EVENT_MOUSE_BUTTON,    /* code = button, value = down              */
    INPUT_EVENT_MOUSE_WHEEL,     /* value = wheel delta                      */
};

enum input_source {
    INPUT_SRC_UNKNOWN = 0,
    INPUT_SRC_PS2_KEYBOARD,
    INPUT_SRC_USB_KEYBOARD,
    INPUT_SRC_USB_MOUSE,
};

struct input_event {
    uint16_t type;     /* enum input_event_type  */
    uint16_t code;     /* scancode / keycode / char / button */
    int32_t  value;    /* press/release / dx / wheel delta   */
    int32_t  value2;   /* dy (mouse move)        */
    uint16_t source;   /* enum input_source      */
    uint16_t _pad;
};

void input_event_init_key(struct input_event *ev, uint16_t code, int pressed);
const char *input_source_name(uint16_t source);

#endif /* MYOS_INPUT_EVENT_H */
