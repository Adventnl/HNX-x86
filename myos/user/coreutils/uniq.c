/* uniq [-c] [file]: collapse adjacent duplicate lines. With -c, prefix each line
 * with its repeat count. Reads stdin when given no file. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"

int main(int argc, char **argv) {
    int count = 0;
    int i = 1;
    if (i < argc && argv[i][0] == '-' && argv[i][1] == 'c' && !argv[i][2]) {
        count = 1;
        i++;
    }
    int fd = 0;
    if (i < argc) {
        fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("uniq: %s: cannot open\n", argv[i]);
            return 1;
        }
    }
    char cur[1024], prev[1024];
    int have_prev = 0;
    unsigned long reps = 0;
    long n;
    while ((n = fdgets(fd, cur, sizeof(cur))) > 0) {
        if (have_prev && strcmp(cur, prev) == 0) {
            reps++;
            continue;
        }
        if (have_prev) {
            if (count) printf("%7lu ", reps);
            write(1, prev, (unsigned long)strlen(prev));
        }
        memcpy(prev, cur, (size_t)strlen(cur) + 1);
        have_prev = 1;
        reps = 1;
    }
    if (have_prev) {
        if (count) printf("%7lu ", reps);
        write(1, prev, (unsigned long)strlen(prev));
    }
    if (fd != 0) {
        close(fd);
    }
    return 0;
}
