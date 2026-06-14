/* blocks: list registered block devices + geometry. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_block_entry b[16];
    int n = blocks(b, 16);
    if (n < 0) {
        print("blocks: error\n");
        return 1;
    }
    printf("%-10s %12s %s\n", "BLOCKDEV", "SECTORS", "SECSIZE");
    for (int i = 0; i < n; i++) {
        printf("%-10s %12lu %u\n", b[i].name,
               (unsigned long)b[i].sectors, b[i].sector_size);
    }
    return 0;
}
