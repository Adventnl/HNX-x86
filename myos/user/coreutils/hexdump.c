/* hexdump <path>: classic offset / hex / ASCII dump. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        print("usage: hexdump <path>\n");
        return 1;
    }
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("hexdump: cannot open '%s'\n", argv[1]);
        return 1;
    }
    unsigned char buf[16];
    long off = 0, n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        printf("%08x  ", (unsigned)off);
        for (long i = 0; i < 16; i++) {
            if (i < n) {
                printf("%02x ", buf[i]);
            } else {
                printf("   ");
            }
        }
        printf(" |");
        for (long i = 0; i < n; i++) {
            char c = (buf[i] >= 32 && buf[i] < 127) ? (char)buf[i] : '.';
            putchar(c);
        }
        printf("|\n");
        off += n;
    }
    close(fd);
    return 0;
}
