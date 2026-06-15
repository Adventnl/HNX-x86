/* keytest: drain any pending unified key events and report them with source. */
#include "stdio.h"
#include "unistd.h"

static const char *src_name(unsigned short s) {
    static const char *n[] = {"unknown","ps2-keyboard","usb-keyboard","usb-mouse"};
    return (s < 4) ? n[s] : "?";
}

int main(void) {
    struct sys_input_event ev;
    int seen = 0;
    while (input_poll(&ev) == 1) {
        if (ev.type == 1 || ev.type == 2 || ev.type == 3 || ev.type == 4) {
            printf("key event: type=%u code=%u value=%d src=%s\n",
                   ev.type, ev.code, ev.value, src_name(ev.source));
            seen++;
        }
        if (seen > 64) {
            break;
        }
    }
    if (seen == 0) {
        print("keytest: no key events pending (press keys to generate them)\n");
    }
    return 0;
}
