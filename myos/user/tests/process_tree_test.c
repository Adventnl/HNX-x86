/* Work Unit B userland test: process tree, waitpid semantics, zombie cleanup.
 * Runs as a ring-3 child of init; prints the markers grepped by
 * verify-process-expanded. */
#include "stdio.h"
#include "unistd.h"
#include "syscall.h"   /* WNOHANG and the SYS_* errno constants */

int main(void) {
    /* ---- process tree: identity + a normal spawn/wait ---- */
    long pid = getpid();
    long ppid = getppid();
    long tid = gettid();

    char *const targv[] = { (char *)"/bin/true.hxe", 0 };
    long cpid = spawn("/bin/true.hxe", targv);
    long code = -1;
    long w = wait_pid(cpid, &code);

    int tree_ok = (pid > 0) && (ppid > 0) && (ppid != pid) && (tid > 0) &&
                  (cpid > 0) && (w == cpid) && (code == 0);
    print(tree_ok ? "[PASS] process tree tests\n" : "[FAIL] process tree tests\n");

    /* ---- waitpid: exit code propagation + WNOHANG with no children ---- */
    char *const fargv[] = { (char *)"/bin/false.hxe", 0 };
    long fpid = spawn("/bin/false.hxe", fargv);
    long fcode = -1;
    long fw = waitpid(fpid, &fcode, 0);
    long none = waitpid(-1, 0, WNOHANG);   /* no remaining children */

    int wait_ok = (fpid > 0) && (fw == fpid) && (fcode == 1) && (none <= 0);
    print(wait_ok ? "[PASS] waitpid tests\n" : "[FAIL] waitpid tests\n");

    /* ---- zombie cleanup: reap once, second reap reports no child ---- */
    char *const zargv[] = { (char *)"/bin/true.hxe", 0 };
    long zpid = spawn("/bin/true.hxe", zargv);
    sleep_ms(20);                          /* let the child exit (zombie) */
    long zcode = -1;
    long zw = waitpid(zpid, &zcode, 0);
    long again = waitpid(zpid, 0, WNOHANG); /* already reaped -> -ECHILD */

    int zombie_ok = (zpid > 0) && (zw == zpid) && (zcode == 0) && (again < 0);
    print(zombie_ok ? "[PASS] zombie cleanup tests\n" : "[FAIL] zombie cleanup tests\n");

    return (tree_ok && wait_ok && zombie_ok) ? 0 : 1;
}
