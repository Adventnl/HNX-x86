/* Mouse event queue: a lock-light ring of relative-motion / button / wheel
 * events, drained by userland (mousetest) via the input syscall. */
#ifndef MYOS_MOUSE_EVENT_H
#define MYOS_MOUSE_EVENT_H

#include "types.h"

/* Mouse button bits. */
#define MOUSE_BTN_LEFT    0x01
#define MOUSE_BTN_RIGHT   0x02
#define MOUSE_BTN_MIDDLE  0x04

struct mouse_event {
    int16_t  dx;
    int16_t  dy;
    int8_t   wheel;
    uint8_t  buttons;     /* current button bitmap */
    uint16_t source;      /* enum input_source     */
};

void mouse_event_init(void);
int  mouse_event_push(const struct mouse_event *ev);   /* 0 ok, -1 full  */
int  mouse_event_pop(struct mouse_event *out);          /* 0 ok, -1 empty */
int  mouse_event_count(void);

#endif /* MYOS_MOUSE_EVENT_H */
