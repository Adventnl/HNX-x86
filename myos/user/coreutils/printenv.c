/* printenv <name> [name...]: print the value of each named environment
 * variable. (MyOS exposes no enumeration API, so a variable name is required.) */
#include "stdio.h"
#include "unistd.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        eprint("usage: printenv <name> [name...]\n");
        return 2;
    }
    int rc = 0;
    char val[256];
    for (int i = 1; i < argc; i++) {
        int r = env_get(argv[i], val);
        if (r < 0) {
            rc = 1;            /* not set: print nothing for this name */
            continue;
        }
        printf("%s\n", val);
    }
    return rc;
}
