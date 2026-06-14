/* MyOS init (PID 1): the first ring-3 program. Prints the banner, runs the
 * userland test matrix, launches the scripted shell, then exits cleanly. The
 * kernel supervisor reaps init and announces the final pass marker. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"

/* Spawn a program (argv = { path }) and wait for it. Returns its exit code. */
static long run(const char *path) {
    char *const argv[] = { (char *)path, (char *)0 };
    long pid = spawn(path, argv);
    if (pid < 0) {
        printf("[init] spawn failed (%ld): %s\n", pid, path);
        return pid;
    }
    long code = 0;
    wait_pid(pid, &code);
    return code;
}

static void cat_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return;
    }
    char buf[256];
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, (unsigned long)n);
    }
    close(fd);
}

int main(void) {
    print("[USER] hello from ring 3\n");
    cat_file("/etc/banner.txt");
    printf("[init] starting, pid=%ld\n", getpid());

    run("/tests/syscall_test.hxe");
    run("/tests/fd_test.hxe");
    run("/tests/vfs_test.hxe");
    run("/tests/spawn_test.hxe");
    run("/tests/fault_test.hxe");   /* faults on purpose; kernel isolates it */

    run("/bin/shell.hxe");          /* scripted session via /dev/console */

    cat_file("/etc/motd.txt");
    print("[init] userland foundation complete\n");
    return 0;
}
