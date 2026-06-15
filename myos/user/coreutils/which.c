/* which <name>...: report the /bin path of each named program if it exists. */
#include "stdio.h"
#include "unistd.h"
#include "string.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        eprint("usage: which <name>...\n");
        return 2;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        char path[256];
        /* Already-qualified path: check as-is. */
        if (strchr(argv[i], '/')) {
            snprintf(path, sizeof(path), "%s", argv[i]);
        } else {
            snprintf(path, sizeof(path), "/bin/%s.hxe", argv[i]);
        }
        struct sys_stat st;
        if (stat(path, &st) == 0 && st.type == 0) {
            printf("%s\n", path);
        } else {
            rc = 1;
        }
    }
    return rc;
}
