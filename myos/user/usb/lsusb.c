/* lsusb: list enumerated USB devices. */
#include "stdio.h"
#include "unistd.h"

static const char *speed_name(unsigned char s) {
    static const char *n[] = {"?","full","low","high","super"};
    return (s < 5) ? n[s] : "?";
}

static const char *class_name(unsigned char c) {
    switch (c) {
    case 0x03: return "HID";
    case 0x08: return "Mass Storage";
    case 0x09: return "Hub";
    default:   return "Device";
    }
}

int main(void) {
    struct sys_usb_entry u[16];
    int n = usb_devices(u, 16);
    if (n < 0) {
        print("lsusb: error\n");
        return 1;
    }
    if (n == 0) {
        print("lsusb: no USB devices\n");
        return 0;
    }
    for (int i = 0; i < n; i++) {
        printf("Slot %u: ID %x:%x %s (%s, %s)\n", u[i].slot, u[i].vendor, u[i].product,
               class_name(u[i].dev_class), speed_name(u[i].speed),
               class_name(u[i].dev_class));
    }
    return 0;
}
