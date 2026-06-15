/* inputtest: confirm the unified input pipeline is queryable from userland —
 * reports device counts and drains any pending key/mouse events. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_hw_info h;
    if (hw_info(&h) != 0) {
        print("inputtest: hw_info error\n");
        return 1;
    }
    printf("input: %u usb device(s), %u hw event(s)\n",
           h.usb_devices, (unsigned)h.hw_events);

    int keys = 0, mice = 0;
    struct sys_input_event ke;
    while (input_poll(&ke) == 1 && keys < 64) {
        keys++;
    }
    struct sys_mouse_event me;
    while (mouse_poll(&me) == 1 && mice < 64) {
        mice++;
    }
    printf("input: drained %d key event(s), %d mouse event(s)\n", keys, mice);
    print("inputtest: unified input pipeline OK\n");
    return 0;
}
