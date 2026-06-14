/* devices: list the driver-core device registry. */
#include "stdio.h"
#include "unistd.h"

static const char *type_name(unsigned int t) {
    static const char *names[] = {
        "bus", "pci", "block", "char", "input", "console", "storage"
    };
    return (t < 7) ? names[t] : "?";
}

int main(void) {
    struct sys_device_entry d[32];
    int n = devices(d, 32);
    if (n < 0) {
        print("devices: error\n");
        return 1;
    }
    printf("%-12s %s\n", "DEVICE", "TYPE");
    for (int i = 0; i < n; i++) {
        printf("%-12s %s\n", d[i].name, type_name(d[i].type));
    }
    return 0;
}
