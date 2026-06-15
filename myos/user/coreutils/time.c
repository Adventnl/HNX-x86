/* time: run a command and report how long it took (wall-clock ms). */
#include "stdio.h"
#include "unistd.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        eprint("usage: time <command> [args...]\n");
        return 2;
    }
    unsigned long start = uptime_ms();
    long pid = spawn(argv[1], &argv[1]);
    if (pid < 0) {
        printf("time: cannot spawn %s\n", argv[1]);
        return 1;
    }
    long code = 0;
    wait_pid(pid, &code);
    unsigned long elapsed = uptime_ms() - start;
    printf("\nreal\t%lums\ncommand exited %ld\n", elapsed, code);
    return (int)code;
}
