/* Path normalization with a small component stack. */
#include "path.h"
#include "inode.h"
#include "syscall_numbers.h"
#include "string.h"

#define MAX_COMPONENTS 32

int path_resolve(const char *cwd, const char *path, char *out, uint64_t out_size) {
    if (!path || !out || out_size < 2) {
        return -SYS_EINVAL;
    }

    /* Component stack of pointers+lengths into a working copy. */
    char work[VFS_PATH_MAX * 2];
    char *w = work;
    uint64_t wcap = sizeof(work);

    /* Seed with cwd unless the path is absolute. */
    uint64_t wlen = 0;
    if (path[0] != '/') {
        const char *base = (cwd && cwd[0]) ? cwd : "/";
        uint64_t bl = strlen(base);
        if (bl >= wcap - 2) {
            return -SYS_ENAMETOOLONG;
        }
        memcpy(w, base, bl);
        wlen = bl;
        w[wlen++] = '/';
    }
    uint64_t pl = strlen(path);
    if (wlen + pl >= wcap) {
        return -SYS_ENAMETOOLONG;
    }
    memcpy(w + wlen, path, pl);
    wlen += pl;
    w[wlen] = 0;

    /* Tokenize on '/', applying . and .. against a component stack. */
    const char *comps[MAX_COMPONENTS];
    uint64_t lens[MAX_COMPONENTS];
    int top = 0;

    uint64_t i = 0;
    while (i < wlen) {
        while (i < wlen && w[i] == '/') {
            i++;
        }
        uint64_t start = i;
        while (i < wlen && w[i] != '/') {
            i++;
        }
        uint64_t clen = i - start;
        if (clen == 0) {
            continue;
        }
        if (clen == 1 && w[start] == '.') {
            continue;
        }
        if (clen == 2 && w[start] == '.' && w[start + 1] == '.') {
            if (top > 0) {
                top--;
            }
            continue;
        }
        if (top >= MAX_COMPONENTS) {
            return -SYS_ENAMETOOLONG;
        }
        comps[top] = &w[start];
        lens[top] = clen;
        top++;
    }

    /* Rebuild "/a/b/c" (or "/" when empty). */
    uint64_t o = 0;
    if (top == 0) {
        out[o++] = '/';
        out[o] = 0;
        return 0;
    }
    for (int c = 0; c < top; c++) {
        if (o + 1 + lens[c] >= out_size) {
            return -SYS_ENAMETOOLONG;
        }
        out[o++] = '/';
        memcpy(out + o, comps[c], lens[c]);
        o += lens[c];
    }
    out[o] = 0;
    return 0;
}

const char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int path_parent(const char *abspath, char *out, uint64_t out_size) {
    uint64_t n = strlen(abspath);
    if (n == 0 || out_size < 2) {
        return -SYS_EINVAL;
    }
    /* Strip the trailing component. */
    while (n > 0 && abspath[n - 1] != '/') {
        n--;
    }
    /* Drop the separating slash (unless we are left with the root). */
    while (n > 1 && abspath[n - 1] == '/') {
        n--;
    }
    if (n == 0) {
        n = 1;   /* root */
    }
    if (n >= out_size) {
        return -SYS_ENAMETOOLONG;
    }
    memcpy(out, abspath, n);
    out[n] = 0;
    return 0;
}
