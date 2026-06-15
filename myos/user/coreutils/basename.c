/* basename <path> [suffix]: strip directory components (and an optional
 * trailing suffix) from a path. */
#include "stdio.h"
#include "unistd.h"
#include "string.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        eprint("usage: basename <path> [suffix]\n");
        return 2;
    }
    char path[256];
    /* Copy so we can trim trailing slashes in place. */
    size_t len = strlen(argv[1]);
    if (len >= sizeof(path)) {
        len = sizeof(path) - 1;
    }
    memcpy(path, argv[1], len);
    path[len] = 0;

    /* Strip trailing slashes (but keep a lone "/"). */
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = 0;
    }
    char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (base[0] == 0) {
        base = "/";
    }
    /* Optional suffix removal. */
    if (argc > 2) {
        size_t bl = strlen(base);
        size_t sl = strlen(argv[2]);
        if (sl && sl < bl && strcmp(base + bl - sl, argv[2]) == 0) {
            base[bl - sl] = 0;
        }
    }
    printf("%s\n", base);
    return 0;
}
