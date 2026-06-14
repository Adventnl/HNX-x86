/* syscall_test: validate the basic syscall return paths and error handling.
 * Prints "[PASS] syscall_test" on success (the marker verify-syscalls greps). */
#include "stdio.h"
#include "unistd.h"
#include "syscall.h"

static int failures;

static void check(int cond, const char *name) {
    if (!cond) {
        failures++;
        printf("[FAIL] %s\n", name);
    }
}

int main(void) {
    print("[test] syscall_test start\n");

    /* invalid syscall number -> -ENOSYS (no panic). */
    check(__syscall(0xBEEF, 0, 0, 0) == -SYS_ENOSYS, "invalid syscall -> -ENOSYS");

    /* bad user pointer to write -> negative error. */
    check(write(1, (const void *)0x1000, 16) < 0, "bad write pointer rejected");

    /* getpid is positive and stable. */
    long p1 = getpid(), p2 = getpid();
    check(p1 > 0 && p1 == p2, "getpid positive + stable");

    /* yield and sleep return 0. */
    check(yield() == 0, "yield returns 0");
    check(sleep_ms(5) == 0, "sleep returns 0");

    /* read length 0 returns 0. */
    char b[4];
    check(read(0, b, 0) == 0, "read len 0 returns 0");

    if (failures == 0) {
        print("[PASS] syscall_test\n");
        return 0;
    }
    print("[FAIL] syscall_test\n");
    return 1;
}
