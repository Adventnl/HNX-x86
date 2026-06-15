/* cp <src> <dst>: copy a file via read/write. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"

int main(int argc, char **argv) {
    if (argc != 3) {
        eprint("usage: cp <src> <dst>\n");
        return 2;
    }
    int in = open(argv[1], O_RDONLY);
    if (in < 0) {
        printf("cp: %s: cannot open\n", argv[1]);
        return 1;
    }
    int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC);
    if (out < 0) {
        printf("cp: %s: cannot create\n", argv[2]);
        close(in);
        return 1;
    }
    char buf[512];
    long n;
    int rc = 0;
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        long off = 0;
        while (off < n) {
            long w = write(out, buf + off, (unsigned long)(n - off));
            if (w <= 0) {
                printf("cp: %s: write error\n", argv[2]);
                rc = 1;
                goto done;
            }
            off += w;
        }
    }
    if (n < 0) {
        rc = 1;
    }
done:
    close(in);
    close(out);
    return rc;
}
