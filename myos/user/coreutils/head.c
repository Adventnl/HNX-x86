/* head [-n N] [files...]: print the first N lines (default 10) of each file or
 * stdin. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

static void head_fd(int fd, long limit) {
    char line[1024];
    long n;
    long count = 0;
    while (count < limit && (n = fdgets(fd, line, sizeof(line))) > 0) {
        write(1, line, (unsigned long)strlen(line));
        count++;
    }
}

int main(int argc, char **argv) {
    long limit = 10;
    int i = 1;
    if (i < argc && argv[i][0] == '-' && argv[i][1] == 'n') {
        if (argv[i][2]) {
            limit = atol(argv[i] + 2);
            i++;
        } else if (i + 1 < argc) {
            limit = atol(argv[i + 1]);
            i += 2;
        } else {
            eprint("head: -n needs an argument\n");
            return 2;
        }
    }
    if (i >= argc) {
        head_fd(0, limit);
        return 0;
    }
    int rc = 0;
    int many = (argc - i) > 1;
    for (; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("head: %s: cannot open\n", argv[i]);
            rc = 1;
            continue;
        }
        if (many) {
            printf("==> %s <==\n", argv[i]);
        }
        head_fd(fd, limit);
        close(fd);
    }
    return rc;
}
