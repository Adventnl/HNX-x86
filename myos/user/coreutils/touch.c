/* touch: create empty files if they do not exist. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        print("usage: touch <file>...\n");
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_CREAT | O_WRONLY);
        if (fd < 0) {
            printf("touch: cannot create '%s'\n", argv[i]);
            rc = 1;
        } else {
            close(fd);
        }
    }
    return rc;
}
