/* hid_test: ring-3 assertion that a USB HID device was enumerated. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_usb_entry u[16];
    int n = usb_devices(u, 16);
    int hid = 0;
    for (int i = 0; i < n; i++) {
        if (u[i].dev_class == 0x03) {
            hid++;
        }
    }
    if (hid < 1) {
        print("[FAIL] hid_test\n");
        return 1;
    }
    print("[PASS] hid_test\n");
    return 0;
}
