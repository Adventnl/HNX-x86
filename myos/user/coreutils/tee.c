/* tee [files...]: copy stdin to stdout and to each named file. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"

#define MAXOUT 8

int main(int argc, char **argv) {
    int outs[MAXOUT];
    int nout = 0;
    for (int i = 1; i < argc && nout < MAXOUT; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) {
            printf("tee: %s: cannot create\n", argv[i]);
            continue;
        }
        outs[nout++] = fd;
    }
    char buf[512];
    long n;
    int rc = 0;
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        write(1, buf, (unsigned long)n);
        for (int k = 0; k < nout; k++) {
            write(outs[k], buf, (unsigned long)n);
        }
    }
    if (n < 0) {
        rc = 1;
    }
    for (int k = 0; k < nout; k++) {
        close(outs[k]);
    }
    return rc;
}
