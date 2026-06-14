/* disk_test: confirm a block device is present + its geometry is sane. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    print("[test] disk_test start\n");
    struct sys_block_entry b[16];
    int n = blocks(b, 16);
    int have_disk = 0;
    for (int i = 0; i < n; i++) {
        if (b[i].sectors > 0 && b[i].sector_size == 512) {
            have_disk = 1;
        }
    }
    if (n >= 1 && have_disk) {
        print("[PASS] disk_test\n");
        return 0;
    }
    print("[FAIL] disk_test\n");
    return 1;
}
