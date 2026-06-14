/* ps: list processes from the kernel process table. */
#include "stdio.h"
#include "unistd.h"

static const char *state_name(unsigned int s) {
    switch (s) {
    case 0: return "NEW";
    case 1: return "READY";
    case 2: return "RUN";
    case 3: return "SLEEP";
    case 4: return "WAIT";
    case 5: return "EXIT";
    case 6: return "FAULT";
    default: return "?";
    }
}

int main(void) {
    struct sys_ps_entry list[64];
    int n = ps(list, 64);
    if (n < 0) {
        print("ps: error\n");
        return 1;
    }
    printf("  PID  PPID STATE  NAME\n");
    for (int i = 0; i < n; i++) {
        printf("%5lu %5lu %-6s %s\n",
               (unsigned long)list[i].pid,
               (unsigned long)list[i].parent_pid,
               state_name(list[i].state),
               list[i].name);
    }
    return 0;
}
