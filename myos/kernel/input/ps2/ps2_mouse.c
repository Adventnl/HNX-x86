/* PS/2 mouse foundation: the second 8042 port is recognized but not yet driven
 * (deferred to the input/HID expansion in Prompt 6). */
#include "ps2_mouse.h"

void ps2_mouse_init(void) {
    /* Intentionally minimal in Prompt 5. */
}
