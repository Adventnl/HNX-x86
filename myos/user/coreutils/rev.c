/* rev [file]: reverse the characters of each line. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"

int main(int argc, char **argv) {
    int fd = 0;
    if (argc > 1) {
        fd = open(argv[1], O_RDONLY);
        if (fd < 0) {
            printf("rev: %s: cannot open\n", argv[1]);
            return 1;
        }
    }
    char line[1024];
    long n;
    while ((n = fdgets(fd, line, sizeof(line))) > 0) {
        size_t len = strlen(line);
        int nl = (len && line[len - 1] == '\n');
        if (nl) {
            len--;
        }
        for (size_t k = 0; k < len; k++) {
            char c = line[len - 1 - k];
            write(1, &c, 1);
        }
        if (nl) {
            write(1, "\n", 1);
        }
    }
    if (fd != 0) {
        close(fd);
    }
    return 0;
}
