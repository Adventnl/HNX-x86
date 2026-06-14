/* readfile <path>: print a file's contents to stdout. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        print("usage: readfile <path>\n");
        return 1;
    }
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("readfile: cannot open '%s'\n", argv[1]);
        return 1;
    }
    char buf[256];
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, (unsigned long)n);
    }
    write(1, "\n", 1);
    close(fd);
    return 0;
}
