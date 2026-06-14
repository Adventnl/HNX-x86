/* cat: concatenate files (or stdin when given no arguments) to stdout. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"

static int cat_fd(int fd) {
    char buf[256];
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, (unsigned long)n);
    }
    return (n < 0) ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return cat_fd(0);
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cat: %s: not found\n", argv[i]);
            rc = 1;
            continue;
        }
        if (cat_fd(fd)) {
            rc = 1;
        }
        close(fd);
    }
    return rc;
}
