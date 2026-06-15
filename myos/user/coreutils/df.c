/* df: list mounted filesystems (mount point and filesystem type). */
#include "stdio.h"
#include "unistd.h"

#define MAXMNT 32

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    struct sys_mount_entry m[MAXMNT];
    int n = mounts(m, MAXMNT);
    if (n < 0) {
        eprint("df: error\n");
        return 1;
    }
    printf("%-24s%-12s\n", "Mounted on", "Type");
    for (int i = 0; i < n; i++) {
        printf("%-24s%-12s\n", m[i].path, m[i].fs);
    }
    return 0;
}
