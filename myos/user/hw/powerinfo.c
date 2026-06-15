/* powerinfo: per-device power state from the driver-core registry. */
#include "stdio.h"
#include "unistd.h"

static const char *power_name(unsigned int p) {
    static const char *n[] = {"D0-active","D1-light","D2-deep","D3-off"};
    return (p < 4) ? n[p] : "?";
}

int main(void) {
    struct sys_device_entry d[64];
    int n = devices(d, 64);
    if (n < 0) {
        print("powerinfo: error\n");
        return 1;
    }
    printf("%-12s %s\n", "DEVICE", "POWER");
    for (int i = 0; i < n; i++) {
        printf("%-12s %s\n", d[i].name, power_name(d[i].power_state));
    }
    return 0;
}
