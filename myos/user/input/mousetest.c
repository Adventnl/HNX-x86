/* mousetest: drain pending mouse events and report movement/buttons/wheel. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_mouse_event ev;
    int seen = 0;
    while (mouse_poll(&ev) == 1) {
        printf("mouse: dx=%d dy=%d wheel=%d buttons=0x%x\n",
               ev.dx, ev.dy, ev.wheel, ev.buttons);
        if (++seen > 64) {
            break;
        }
    }
    if (seen == 0) {
        print("mousetest: no mouse events pending (move the mouse to generate them)\n");
    }
    return 0;
}
