/* Work Unit B userland test: syscall argument validation under stress — bad
 * fds, bad pointers, bad paths and invalid syscall numbers must all return the
 * right negative errno and never crash the kernel.
 * Marker: "[PASS] syscall stress tests". */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "syscall.h"

#define BAD_PTR ((void *)0x0000123456789000ULL)  /* unmapped, canonical-low */

int main(void) {
    int ok = 1;
    char buf[16];

    for (int round = 0; round < 64; round++) {
        /* bad fd */
        ok &= (read(999, buf, sizeof(buf)) < 0);
        ok &= (close(999) < 0);
        ok &= (lseek(999, 0, SEEK_SET) < 0);

        /* bad user pointer on a copy-in path */
        ok &= (write(1, BAD_PTR, 16) == -SYS_EFAULT);
        /* bad user pointer on a path argument */
        ok &= (open((const char *)BAD_PTR, 0) == -SYS_EFAULT);

        /* nonexistent path */
        ok &= (open("/no/such/file", 0) < 0);

        /* invalid syscall number */
        ok &= (__syscall(4242, 0, 0, 0) == -SYS_ENOSYS);

        /* dup of bad fd */
        ok &= (dup(999) < 0);
        /* waitpid for a non-child */
        ok &= (waitpid(99999, 0, WNOHANG) < 0);
    }

    /* A valid call after all the abuse still works (kernel state intact). */
    ok &= (getpid() > 0);

    print(ok ? "[PASS] syscall stress tests\n" : "[FAIL] syscall stress tests\n");
    return ok ? 0 : 1;
}
