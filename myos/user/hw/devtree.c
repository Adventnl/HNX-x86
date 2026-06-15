/* devtree: a simple device tree — PCI/driver-core devices, then USB devices. */
#include "stdio.h"
#include "unistd.h"

static const char *type_name(unsigned int t) {
    static const char *n[] = {"bus","pci","block","char","input","console","storage"};
    return (t < 7) ? n[t] : "?";
}

int main(void) {
    struct sys_device_entry d[64];
    int n = devices(d, 64);
    if (n < 0) {
        print("devtree: error\n");
        return 1;
    }
    print("system\n");
    for (int i = 0; i < n; i++) {
        printf("  +- %-12s [%s] %s\n", d[i].name, type_name(d[i].type),
               d[i].driver[0] ? d[i].driver : "");
    }

    struct sys_usb_entry u[16];
    int m = usb_devices(u, 16);
    if (m > 0) {
        print("  +- usb (xhci root hub)\n");
        for (int i = 0; i < m; i++) {
            printf("     +- %-6s vid=%x pid=%x class=%x\n",
                   u[i].name, u[i].vendor, u[i].product, u[i].dev_class);
        }
    }
    return 0;
}
