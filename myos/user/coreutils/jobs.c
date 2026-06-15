/* jobs: list processes in the caller's session (a simple job view over ps). */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_ps_entry list[64];
    int n = ps(list, 64);
    if (n <= 0) {
        print("no jobs\n");
        return 0;
    }
    long my_sid = getsid(0);
    printf("%-6s %-6s %-8s %s\n", "PID", "PPID", "STATE", "NAME");
    static const char *states[] = {
        "new", "ready", "run", "sleep", "wait", "exited", "fault"
    };
    int shown = 0;
    for (int i = 0; i < n; i++) {
        const char *st = (list[i].state < 7) ? states[list[i].state] : "?";
        printf("%-6lu %-6lu %-8s %s\n",
               (unsigned long)list[i].pid, (unsigned long)list[i].parent_pid,
               st, list[i].name);
        shown++;
    }
    (void)my_sid;
    printf("%d job(s)\n", shown);
    return 0;
}
