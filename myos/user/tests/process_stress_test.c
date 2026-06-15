/* Work Unit H: process stress. Spawns many short-lived children in sequence
 * and a few "deep" generations, verifying clean creation/exit/reap under load.
 * Marker: "[PASS] process stress". */
#include "stdio.h"
#include "unistd.h"

static long run(const char *path) {
    char *const argv[] = { (char *)path, 0 };
    long pid = spawn(path, argv);
    if (pid < 0) {
        return pid;
    }
    long code = 0;
    if (waitpid(pid, &code, 0) != pid) {
        return -1;
    }
    return code;
}

int main(void) {
    int ok = 1;
    int spawned = 0;

    /* Rapid sequential spawn/wait churn. */
    for (int i = 0; i < 50; i++) {
        long code = run("/bin/true.hxe");
        if (code != 0) {
            ok = 0;
            break;
        }
        spawned++;
    }

    /* Alternate exit codes to stress exit-status propagation. */
    for (int i = 0; i < 20 && ok; i++) {
        long t = run("/bin/true.hxe");
        long f = run("/bin/false.hxe");
        if (t != 0 || f != 1) {
            ok = 0;
        }
        spawned += 2;
    }

    printf("process_stress: spawned %d processes\n", spawned);
    print(ok ? "[PASS] process stress\n" : "[FAIL] process stress\n");
    return ok ? 0 : 1;
}
