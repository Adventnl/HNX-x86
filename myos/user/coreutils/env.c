/* env [NAME=VALUE]... [command [args...]]
 *   With a command: set the given NAME=VALUE pairs in the environment, then run
 *   the command (which inherits the environment).
 *   With a single NAME (no '='): print that variable's value.
 *   With no command and only assignments: just apply them. */
#include "stdio.h"
#include "unistd.h"
#include "string.h"

int main(int argc, char **argv) {
    int i = 1;
    /* Leading NAME=VALUE assignments. */
    for (; i < argc && strchr(argv[i], '='); i++) {
        if (env_set(argv[i]) < 0) {
            printf("env: cannot set %s\n", argv[i]);
            return 1;
        }
    }
    if (i >= argc) {
        return 0;          /* assignments only */
    }
    /* If a single bare name remains and there is nothing after it, treat it as
     * a lookup (env VAR). */
    if (i + 1 == argc && !strchr(argv[i], '=')) {
        char val[256];
        if (env_get(argv[i], val) < 0) {
            return 1;
        }
        printf("%s\n", val);
        return 0;
    }
    /* Otherwise spawn the command with its arguments. */
    long pid = spawn(argv[i], &argv[i]);
    if (pid < 0) {
        printf("env: cannot run %s\n", argv[i]);
        return 1;
    }
    long code = 0;
    wait_pid(pid, &code);
    return (int)code;
}
