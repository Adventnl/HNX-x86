/* du [path]: report disk usage (bytes) of a file or directory tree. Recurses
 * directories via readdir + stat. Default path is ".". */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"

#define PATHMAX 512

static void join(char *out, size_t cap, const char *dir, const char *name) {
    size_t dl = strlen(dir);
    if (dl && dir[dl - 1] == '/') {
        snprintf(out, cap, "%s%s", dir, name);
    } else {
        snprintf(out, cap, "%s/%s", dir, name);
    }
}

/* Returns total bytes under `path` (and prints per-directory subtotals). */
static unsigned long long du_walk(const char *path) {
    struct sys_stat st;
    if (stat(path, &st) < 0) {
        printf("du: %s: cannot stat\n", path);
        return 0;
    }
    if (st.type != 1) {            /* not a directory: leaf file */
        return st.size;
    }
    unsigned long long total = 0;
    int fd = open(path, O_DIRECTORY);
    if (fd < 0) {
        return st.size;
    }
    struct sys_dirent de;
    int guard = 0;
    while (readdir(fd, &de) == 1 && guard++ < 4096) {
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
            continue;
        }
        char child[PATHMAX];
        join(child, sizeof(child), path, de.name);
        total += du_walk(child);
    }
    close(fd);
    printf("%10llu  %s\n", total, path);
    return total;
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : ".";
    du_walk(path);
    return 0;
}
