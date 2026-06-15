/* usbinfo: detailed per-device USB descriptor summary. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_usb_entry u[16];
    int n = usb_devices(u, 16);
    if (n < 0) {
        print("usbinfo: error\n");
        return 1;
    }
    printf("%d USB device(s)\n", n);
    for (int i = 0; i < n; i++) {
        printf("device %u:\n", u[i].slot);
        printf("  vendor   : 0x%x\n", u[i].vendor);
        printf("  product  : 0x%x\n", u[i].product);
        printf("  class    : 0x%x.%x proto 0x%x\n",
               u[i].dev_class, u[i].dev_subclass, u[i].dev_protocol);
        printf("  speed    : %u\n", u[i].speed);
    }
    return 0;
}
