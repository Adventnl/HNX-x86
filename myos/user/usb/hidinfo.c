/* hidinfo: list USB HID devices (keyboard/mouse) and their boot protocol. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_usb_entry u[16];
    int n = usb_devices(u, 16);
    if (n < 0) {
        print("hidinfo: error\n");
        return 1;
    }
    int hid = 0;
    for (int i = 0; i < n; i++) {
        if (u[i].dev_class != 0x03) {
            continue;
        }
        hid++;
        const char *kind = (u[i].hid_type == 2) ? "mouse"
                         : (u[i].hid_type == 1) ? "keyboard" : "hid";
        printf("HID %s: slot %u id %x:%x boot-proto=%u\n",
               kind, u[i].slot, u[i].vendor, u[i].product, u[i].dev_protocol);
    }
    if (hid == 0) {
        print("hidinfo: no HID devices\n");
    }
    return 0;
}
