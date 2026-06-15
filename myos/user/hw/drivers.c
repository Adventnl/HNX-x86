/* drivers: list registered devices and their bound driver + lifecycle state. */
#include "stdio.h"
#include "unistd.h"

static const char *type_name(unsigned int t) {
    static const char *n[] = {"bus","pci","block","char","input","console","storage"};
    return (t < 7) ? n[t] : "?";
}

static const char *state_name(unsigned int s) {
    static const char *n[] = {"discovered","matched","initialized","active",
                              "suspended","failed","removed"};
    return (s < 7) ? n[s] : "?";
}

int main(void) {
    struct sys_device_entry d[64];
    int n = devices(d, 64);
    if (n < 0) {
        print("drivers: error\n");
        return 1;
    }
    printf("%-12s %-8s %-12s %s\n", "DEVICE", "TYPE", "STATE", "DRIVER");
    for (int i = 0; i < n; i++) {
        printf("%-12s %-8s %-12s %s\n", d[i].name, type_name(d[i].type),
               state_name(d[i].state), d[i].driver[0] ? d[i].driver : "-");
    }
    return 0;
}
