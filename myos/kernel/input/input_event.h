/* Generic input event (keyboard now; mouse/HID later). */
#ifndef MYOS_INPUT_EVENT_H
#define MYOS_INPUT_EVENT_H

#include "types.h"

enum input_event_type {
    INPUT_EV_KEY = 1,
};

struct input_event {
    uint16_t type;     /* enum input_event_type */
    uint16_t code;     /* scancode / keycode */
    int32_t  value;    /* 1 = press, 0 = release */
};

void input_event_init_key(struct input_event *ev, uint16_t code, int pressed);

#endif /* MYOS_INPUT_EVENT_H */
