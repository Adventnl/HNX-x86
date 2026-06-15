/* usbtest: assert at least one USB device enumerated with a valid descriptor. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_usb_entry u[16];
    int n = usb_devices(u, 16);
    if (n <= 0) {
        print("[FAIL] usbtest: no USB devices\n");
        return 1;
    }
    for (int i = 0; i < n; i++) {
        if (u[i].vendor == 0) {
            print("[FAIL] usbtest: invalid descriptor\n");
            return 1;
        }
    }
    printf("[PASS] usbtest: %d device(s)\n", n);
    return 0;
}
