/* mounts: list the VFS mount table. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_mount_entry m[8];
    int n = mounts(m, 8);
    if (n < 0) {
        print("mounts: error\n");
        return 1;
    }
    printf("%-10s %s\n", "MOUNT", "FS");
    for (int i = 0; i < n; i++) {
        printf("%-10s %s\n", m[i].path, m[i].fs);
    }
    return 0;
}
