/* find [path] [-name pattern]: recursively print every path under `path`
 * (default "."). With -name, only print entries whose basename matches the
 * pattern exactly (no globbing). */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"

#define PATHMAX 512

static const char *g_name;

static void join(char *out, size_t cap, const char *dir, const char *name) {
    size_t dl = strlen(dir);
    if (dl && dir[dl - 1] == '/') {
        snprintf(out, cap, "%s%s", dir, name);
    } else {
        snprintf(out, cap, "%s/%s", dir, name);
    }
}

static const char *base_of(const char *p) {
    const char *b = strrchr(p, '/');
    return b ? b + 1 : p;
}

static void emit(const char *path) {
    if (!g_name || strcmp(base_of(path), g_name) == 0) {
        printf("%s\n", path);
    }
}

static void walk(const char *path) {
    emit(path);
    struct sys_stat st;
    if (stat(path, &st) < 0 || st.type != 1) {
        return;
    }
    int fd = open(path, O_DIRECTORY);
    if (fd < 0) {
        return;
    }
    struct sys_dirent de;
    int guard = 0;
    while (readdir(fd, &de) == 1 && guard++ < 4096) {
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
            continue;
        }
        char child[PATHMAX];
        join(child, sizeof(child), path, de.name);
        walk(child);
    }
    close(fd);
}

int main(int argc, char **argv) {
    const char *path = ".";
    int i = 1;
    if (i < argc && argv[i][0] != '-') {
        path = argv[i++];
    }
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-name") == 0 && i + 1 < argc) {
            g_name = argv[++i];
        } else {
            eprint("usage: find [path] [-name pattern]\n");
            return 2;
        }
    }
    walk(path);
    return 0;
}
