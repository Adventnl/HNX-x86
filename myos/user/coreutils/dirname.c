/* dirname <path>: print the directory portion of a path, or "." if none. */
#include "stdio.h"
#include "unistd.h"
#include "string.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        eprint("usage: dirname <path>\n");
        return 2;
    }
    char path[256];
    size_t len = strlen(argv[1]);
    if (len >= sizeof(path)) {
        len = sizeof(path) - 1;
    }
    memcpy(path, argv[1], len);
    path[len] = 0;

    /* Strip trailing slashes. */
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = 0;
    }
    char *slash = strrchr(path, '/');
    if (!slash) {
        printf(".\n");
        return 0;
    }
    if (slash == path) {
        printf("/\n");        /* root */
        return 0;
    }
    /* Trim trailing slashes of the directory part too. */
    *slash = 0;
    size_t dl = strlen(path);
    while (dl > 1 && path[dl - 1] == '/') {
        path[--dl] = 0;
    }
    printf("%s\n", path);
    return 0;
}
