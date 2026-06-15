/* Work Unit B userland test: file-descriptor duplication (dup/dup2/fcntl) and
 * the fd-table exhaustion path. Marker: "[PASS] fd duplication tests". */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"
#include "syscall.h"   /* F_* fcntl commands */

int main(void) {
    int ok = 1;

    /* dup of stdout yields a new, distinct, working fd. */
    int d = dup(1);
    ok &= (d >= 3);
    if (d >= 0) {
        long w = write(d, "", 0);   /* zero-length write succeeds */
        ok &= (w == 0);
    }

    /* dup2 to a chosen number returns that number. */
    int target = 10;
    int r = dup2(1, target);
    ok &= (r == target);
    ok &= (write(target, "", 0) == 0);

    /* dup of a bad fd fails with -EBADF. */
    ok &= (dup(999) < 0);
    ok &= (dup2(999, 11) < 0);

    /* fcntl(F_DUPFD) behaves like dup. */
    int fd2 = fcntl(1, F_DUPFD, 0);
    ok &= (fd2 >= 3);

    /* fcntl(F_GETFL) returns the file's flags without error. */
    int fl = fcntl(1, F_GETFL, 0);
    ok &= (fl >= 0);

    /* Exhaust the fd table with dups, then confirm it refuses more. */
    int got = 0;
    for (int i = 0; i < 64; i++) {
        int n = dup(1);
        if (n < 0) {
            break;
        }
        got++;
    }
    ok &= (got > 0);              /* allocated several */
    ok &= (dup(1) < 0);          /* now exhausted -> -EMFILE */

    print(ok ? "[PASS] fd duplication tests\n" : "[FAIL] fd duplication tests\n");
    return ok ? 0 : 1;
}
