/* Mouse device model: tracks accumulated position + button state and turns
 * relative HID reports into queued mouse events. */
#ifndef MYOS_MOUSE_H
#define MYOS_MOUSE_H

#include "types.h"

struct mouse_state {
    int32_t x, y;          /* accumulated position */
    uint8_t buttons;
    int32_t wheel;
};

void mouse_init(void);

/* Apply one relative report. Pushes the appropriate mouse events and updates the
 * accumulated state. `source` is an enum input_source. */
void mouse_process(int dx, int dy, uint8_t buttons, int wheel, uint16_t source);

void mouse_get_state(struct mouse_state *out);

#endif /* MYOS_MOUSE_H */
