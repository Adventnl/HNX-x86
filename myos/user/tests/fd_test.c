/* fd_test: exercise the fd table + /dev/null + /dev/zero.
 * Prints "[PASS] fd_test" on success (greped by verify-vfs). */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"

static int failures;

static void check(int cond, const char *name) {
    if (!cond) {
        failures++;
        printf("[FAIL] %s\n", name);
    }
}

int main(void) {
    print("[test] fd_test start\n");

    /* /dev/null: write swallows, read is EOF. */
    int n = open("/dev/null", O_RDWR);
    check(n >= 0, "open /dev/null");
    check(write(n, "hello", 5) == 5, "write /dev/null returns len");
    char buf[8];
    check(read(n, buf, sizeof(buf)) == 0, "read /dev/null EOF");
    check(close(n) == 0, "close /dev/null");

    /* /dev/zero: read yields zeroes. */
    int z = open("/dev/zero", O_RDONLY);
    check(z >= 0, "open /dev/zero");
    memset(buf, 0xAA, sizeof(buf));
    long r = read(z, buf, sizeof(buf));
    check(r == (long)sizeof(buf), "read /dev/zero returns len");
    int allzero = 1;
    for (unsigned i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0) {
            allzero = 0;
        }
    }
    check(allzero, "/dev/zero yields zeroes");

    /* fd allocation: distinct descriptors; lowest-free reuse after close. */
    int a = open("/dev/zero", O_RDONLY);
    int b = open("/dev/zero", O_RDONLY);
    check(a >= 0 && b >= 0 && a != b, "distinct fds");
    close(a);
    int c = open("/dev/zero", O_RDONLY);
    check(c == a, "lowest-free fd reused");
    close(b);
    close(c);
    close(z);

    /* closing an unopened fd fails. */
    check(close(999) < 0, "close invalid fd fails");

    if (failures == 0) {
        print("[PASS] fd_test\n");
        return 0;
    }
    print("[FAIL] fd_test\n");
    return 1;
}
