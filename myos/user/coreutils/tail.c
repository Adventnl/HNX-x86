/* tail [-n N] [files...]: print the last N lines (default 10) of each file or
 * stdin. Buffers lines in a fixed ring so it never needs the whole file. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

#define MAXKEEP 256
#define LINEW   1024

static char g_ring[MAXKEEP][LINEW];

static void tail_fd(int fd, long limit) {
    if (limit <= 0) {
        return;
    }
    if (limit > MAXKEEP) {
        limit = MAXKEEP;
    }
    char line[LINEW];
    long n;
    long total = 0;
    while ((n = fdgets(fd, line, sizeof(line))) > 0) {
        long slot = total % limit;
        memcpy(g_ring[slot], line, (size_t)strlen(line) + 1);
        total++;
    }
    long start = (total > limit) ? total - limit : 0;
    for (long k = start; k < total; k++) {
        long slot = k % limit;
        write(1, g_ring[slot], (unsigned long)strlen(g_ring[slot]));
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
            eprint("tail: -n needs an argument\n");
            return 2;
        }
    }
    if (i >= argc) {
        tail_fd(0, limit);
        return 0;
    }
    int rc = 0;
    int many = (argc - i) > 1;
    for (; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("tail: %s: cannot open\n", argv[i]);
            rc = 1;
            continue;
        }
        if (many) {
            printf("==> %s <==\n", argv[i]);
        }
        tail_fd(fd, limit);
        close(fd);
    }
    return rc;
}
