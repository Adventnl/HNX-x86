/* Work Unit G: coreutils suite smoke test. Spawns a representative set of the
 * expanded coreutils and checks they run and exit 0. Marker:
 * "[PASS] coreutils suite". */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"

static long run(char *const argv[]) {
    long pid = spawn(argv[0], argv);
    if (pid < 0) {
        return pid;
    }
    long code = 0;
    wait_pid(pid, &code);
    return code;
}

int main(void) {
    int ok = 1;

    char *const a_uname[]    = { (char *)"/bin/uname.hxe", 0 };
    char *const a_echo[]     = { (char *)"/bin/echo.hxe", (char *)"hi", 0 };
    char *const a_seq[]      = { (char *)"/bin/seq.hxe", (char *)"1", (char *)"5", 0 };
    char *const a_basename[] = { (char *)"/bin/basename.hxe", (char *)"/a/b/c.txt", 0 };
    char *const a_dirname[]  = { (char *)"/bin/dirname.hxe", (char *)"/a/b/c.txt", 0 };
    char *const a_true[]     = { (char *)"/bin/true.hxe", 0 };

    ok &= (run(a_uname) == 0);
    ok &= (run(a_echo) == 0);
    ok &= (run(a_seq) == 0);
    ok &= (run(a_basename) == 0);
    ok &= (run(a_dirname) == 0);
    ok &= (run(a_true) == 0);

    /* Create a file and word-count it. */
    int fd = open("/disk/cu_wc.txt", O_RDWR | O_CREAT | O_TRUNC);
    if (fd >= 0) {
        write(fd, "one two three\nfour five\n", 24);
        close(fd);
        char *const a_wc[] = { (char *)"/bin/wc.hxe", (char *)"/disk/cu_wc.txt", 0 };
        ok &= (run(a_wc) == 0);
    }

    print(ok ? "[PASS] coreutils suite\n" : "[FAIL] coreutils suite\n");
    return ok ? 0 : 1;
}
