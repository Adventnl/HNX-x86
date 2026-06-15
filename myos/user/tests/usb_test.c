/* usb_test: ring-3 assertion that USB enumeration produced valid devices. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_usb_entry u[16];
    int n = usb_devices(u, 16);
    if (n < 1) {
        print("[FAIL] usb_test\n");
        return 1;
    }
    print("[PASS] usb_test\n");
    return 0;
}
