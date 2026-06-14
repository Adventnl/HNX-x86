/* lspci: list PCI functions from the device registry. */
#include "stdio.h"
#include "unistd.h"

#define DEV_TYPE_PCI 1

int main(void) {
    struct sys_device_entry d[32];
    int n = devices(d, 32);
    if (n < 0) {
        print("lspci: error\n");
        return 1;
    }
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (d[i].type == DEV_TYPE_PCI) {
            printf("%s\n", d[i].name);
            count++;
        }
    }
    if (count == 0) {
        print("lspci: no PCI devices\n");
    }
    return 0;
}
