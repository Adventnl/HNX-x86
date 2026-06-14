/* spawn_test: spawn a child, wait for it, and confirm spawn errors are reported.
 * Prints "[PASS] spawn_test" on success (greped by verify-process). */
#include "stdio.h"
#include "unistd.h"

static int failures;

static void check(int cond, const char *name) {
    if (!cond) {
        failures++;
        printf("[FAIL] %s\n", name);
    }
}

int main(void) {
    print("[test] spawn_test start\n");

    char *const argv[] = { (char *)"/bin/hello.hxe", (char *)0 };
    long pid = spawn("/bin/hello.hxe", argv);
    check(pid > 0, "spawn /bin/hello.hxe");

    long code = -1;
    long w = wait_pid(pid, &code);
    check(w == pid, "wait returns child pid");
    check(code == 0, "child exited 0");

    /* spawning a missing program fails cleanly. */
    char *const bad[] = { (char *)"/bin/does_not_exist.hxe", (char *)0 };
    check(spawn("/bin/does_not_exist.hxe", bad) < 0, "spawn missing program fails");

    if (failures == 0) {
        print("[PASS] spawn_test\n");
        return 0;
    }
    print("[FAIL] spawn_test\n");
    return 1;
}
