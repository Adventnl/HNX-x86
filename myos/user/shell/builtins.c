/* cd / exit builtins (they must mutate the shell process itself). */
#include "builtins.h"
#include "stdio.h"
#include "unistd.h"
#include "string.h"

int builtin_try(int argc, char *argv[], int *want_exit) {
    if (strcmp(argv[0], "exit") == 0) {
        *want_exit = 1;
        return 1;
    }
    if (strcmp(argv[0], "cd") == 0) {
        const char *target = (argc > 1) ? argv[1] : "/";
        if (chdir(target) < 0) {
            printf("cd: %s: no such directory\n", target);
        }
        return 1;
    }
    return 0;
}
