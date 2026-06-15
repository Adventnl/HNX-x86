#include "input_event.h"

void input_event_init_key(struct input_event *ev, uint16_t code, int pressed) {
    ev->type = INPUT_EV_KEY;
    ev->code = code;
    ev->value = pressed ? 1 : 0;
    ev->value2 = 0;
    ev->source = INPUT_SRC_UNKNOWN;
    ev->_pad = 0;
}

const char *input_source_name(uint16_t source) {
    switch (source) {
    case INPUT_SRC_PS2_KEYBOARD: return "ps2-keyboard";
    case INPUT_SRC_USB_KEYBOARD: return "usb-keyboard";
    case INPUT_SRC_USB_MOUSE:    return "usb-mouse";
    default:                     return "unknown";
    }
}
