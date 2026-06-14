/* lsblk: list block devices (alias view of `blocks`). */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_block_entry b[16];
    int n = blocks(b, 16);
    if (n < 0) {
        print("lsblk: error\n");
        return 1;
    }
    for (int i = 0; i < n; i++) {
        unsigned long mib = (unsigned long)(b[i].sectors * b[i].sector_size / (1024 * 1024));
        printf("%-10s %lu sectors (%lu MiB)\n", b[i].name,
               (unsigned long)b[i].sectors, mib);
    }
    return 0;
}
