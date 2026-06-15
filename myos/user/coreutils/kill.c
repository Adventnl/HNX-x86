/* kill: send a signal number to a pid. Usage: kill [-SIG] <pid>
 * Signal delivery is a foundation (the kernel records the pending signal). */
#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        eprint("usage: kill [-signal] <pid>\n");
        return 2;
    }
    int sig = 15;   /* SIGTERM-ish default */
    int argi = 1;
    if (argv[1][0] == '-') {
        sig = atoi(argv[1] + 1);
        argi = 2;
    }
    if (argi >= argc) {
        eprint("kill: missing pid\n");
        return 2;
    }
    long pid = atoi(argv[argi]);
    int r = kill(pid, sig);
    if (r < 0) {
        printf("kill: (%ld) - no such process\n", pid);
        return 1;
    }
    printf("kill: signal %d posted to pid %ld\n", sig, pid);
    return 0;
}
