/* User-side syscall conformance test (Prompt 4). Exits 0 only if every check
 * passes; the kernel supervisor captures the exit code. */
#include "syscall.h"
#include "stdlib.h"

static int failures = 0;

static void check(int cond, const char *name) {
    print(cond ? "[PASS] " : "[FAIL] ");
    print(name);
    print("\n");
    if (!cond) {
        failures++;
    }
}

int main(void) {
    print("[TEST] syscall_test start\n");

    /* Invalid syscall number -> -ENOSYS (no panic). */
    check(usys(0xBEEF, 0, 0, 0) == -SYS_ENOSYS, "invalid syscall returns error");

    /* Bad write pointer (unmapped user address) -> negative error. */
    check(sys_write(1, (const void *)0x1000, 16) < 0, "bad write pointer returns error");

    /* read with length 0 -> 0. */
    char buf[8];
    check(sys_read(0, buf, 0) == 0, "read length 0 returns 0");

    /* getpid is stable and non-negative. */
    long p1 = sys_getpid();
    long p2 = sys_getpid();
    check(p1 >= 0 && p1 == p2, "getpid stable");

    /* yield and sleep return success. */
    check(sys_yield() == 0, "yield returns");
    check(sys_sleep(10) == 0, "sleep returns after ticks advanced");

    if (failures == 0) {
        print("[USER] syscall_test all checks passed\n");
    }
    return failures == 0 ? 0 : 1;   /* exit code captured by the supervisor */
}
