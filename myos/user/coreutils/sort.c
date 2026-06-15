/* sort [-r] [-u] [file]: sort lines lexicographically. -r reverses, -u drops
 * adjacent duplicates after sorting. Reads stdin when given no file. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

#define MAXLINES 1024
#define LINEW    1024

static char  g_lines[MAXLINES][LINEW];
static char *g_idx[MAXLINES];
static int   g_reverse;

static int cmp_lines(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    int r = strcmp(sa, sb);
    return g_reverse ? -r : r;
}

int main(int argc, char **argv) {
    int uniq = 0;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        for (const char *p = argv[i] + 1; *p; p++) {
            if (*p == 'r') g_reverse = 1;
            else if (*p == 'u') uniq = 1;
            else { eprint("sort: unknown flag\n"); return 2; }
        }
    }
    int fd = 0;
    if (i < argc) {
        fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("sort: %s: cannot open\n", argv[i]);
            return 1;
        }
    }
    int count = 0;
    char line[LINEW];
    long n;
    while (count < MAXLINES && (n = fdgets(fd, line, sizeof(line))) > 0) {
        /* Strip a trailing newline so comparison ignores it; re-added on print. */
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n') {
            line[len - 1] = 0;
        }
        memcpy(g_lines[count], line, (size_t)strlen(line) + 1);
        g_idx[count] = g_lines[count];
        count++;
    }
    if (fd != 0) {
        close(fd);
    }
    qsort(g_idx, (size_t)count, sizeof(char *), cmp_lines);
    for (int k = 0; k < count; k++) {
        if (uniq && k > 0 && strcmp(g_idx[k], g_idx[k - 1]) == 0) {
            continue;
        }
        print(g_idx[k]);
        write(1, "\n", 1);
    }
    return 0;
}
