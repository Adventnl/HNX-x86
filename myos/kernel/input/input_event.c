#include "input_event.h"

void input_event_init_key(struct input_event *ev, uint16_t code, int pressed) {
    ev->type = INPUT_EV_KEY;
    ev->code = code;
    ev->value = pressed ? 1 : 0;
}
