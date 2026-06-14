/* rm: remove files (and empty directories). */
#include "stdio.h"
#include "unistd.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        print("usage: rm <path>...\n");
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (unlink(argv[i]) < 0) {
            printf("rm: cannot remove '%s'\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}
