/* cmp <file1> <file2>: report the first byte (and line) where two files differ,
 * or print nothing and exit 0 if identical. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"

int main(int argc, char **argv) {
    if (argc != 3) {
        eprint("usage: cmp <file1> <file2>\n");
        return 2;
    }
    int a = open(argv[1], O_RDONLY);
    if (a < 0) {
        printf("cmp: %s: cannot open\n", argv[1]);
        return 2;
    }
    int b = open(argv[2], O_RDONLY);
    if (b < 0) {
        printf("cmp: %s: cannot open\n", argv[2]);
        close(a);
        return 2;
    }
    char ba[512], bb[512];
    unsigned long pos = 0;
    unsigned long line = 1;
    int rc = 0;
    for (;;) {
        long na = read(a, ba, sizeof(ba));
        long nb = read(b, bb, sizeof(bb));
        if (na < 0 || nb < 0) {
            rc = 2;
            break;
        }
        long m = (na < nb) ? na : nb;
        for (long k = 0; k < m; k++) {
            pos++;
            if (ba[k] != bb[k]) {
                printf("%s %s differ: byte %lu, line %lu\n",
                       argv[1], argv[2], pos, line);
                rc = 1;
                goto done;
            }
            if (ba[k] == '\n') {
                line++;
            }
        }
        if (na != nb) {
            const char *shorter = (na < nb) ? argv[1] : argv[2];
            printf("cmp: EOF on %s\n", shorter);
            rc = 1;
            break;
        }
        if (na == 0) {
            break;       /* both at EOF, equal */
        }
    }
done:
    close(a);
    close(b);
    return rc;
}
