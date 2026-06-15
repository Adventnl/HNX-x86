/* nl [file]: number nonblank lines of a file or stdin. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"

int main(int argc, char **argv) {
    int fd = 0;
    if (argc > 1) {
        fd = open(argv[1], O_RDONLY);
        if (fd < 0) {
            printf("nl: %s: cannot open\n", argv[1]);
            return 1;
        }
    }
    char line[1024];
    long n;
    unsigned long num = 0;
    while ((n = fdgets(fd, line, sizeof(line))) > 0) {
        size_t len = strlen(line);
        int blank = (len == 0) || (len == 1 && line[0] == '\n');
        if (!blank) {
            num++;
            printf("%6lu\t", num);
        } else {
            printf("      \t");
        }
        write(1, line, (unsigned long)len);
    }
    if (fd != 0) {
        close(fd);
    }
    return 0;
}
