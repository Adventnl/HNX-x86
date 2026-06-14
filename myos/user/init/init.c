/* MyOS init (PID 1): runs the userland test matrix (Prompt 4 + Prompt 5), the
 * scripted + interactive shells, then exits. The kernel supervisor reaps init
 * and prints the final pass marker. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"

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

static long run_argv(char *const argv[]) {
    long pid = spawn(argv[0], argv);
    if (pid < 0) {
        printf("[init] spawn failed: %s\n", argv[0]);
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

    /* Prompt 4 user test matrix. */
    run("/tests/syscall_test.hxe");
    run("/tests/fd_test.hxe");
    run("/tests/vfs_test.hxe");
    run("/tests/spawn_test.hxe");
    run("/tests/fault_test.hxe");      /* faults; kernel isolates it */

    /* Prompt 5: expanded coreutils smoke (storage + introspection). */
    int ok = 1;
    char *const a_mkdir[]  = { "/bin/mkdir.hxe", "/disk/cu", 0 };
    char *const a_write[]  = { "/bin/writefile.hxe", "/disk/cu/a.txt", "hello-coreutils", 0 };
    char *const a_read[]   = { "/bin/readfile.hxe", "/disk/cu/a.txt", 0 };
    char *const a_ls[]     = { "/bin/ls.hxe", "/disk", 0 };
    char *const a_stat[]   = { "/bin/stat.hxe", "/disk/cu/a.txt", 0 };
    char *const a_hex[]    = { "/bin/hexdump.hxe", "/disk/cu/a.txt", 0 };
    ok &= (run_argv(a_mkdir) == 0);
    ok &= (run_argv(a_write) == 0);
    ok &= (run_argv(a_read) == 0);
    ok &= (run_argv(a_ls) == 0);
    ok &= (run_argv(a_stat) == 0);
    ok &= (run_argv(a_hex) == 0);
    ok &= (run("/bin/mounts.hxe") == 0);
    ok &= (run("/bin/devices.hxe") == 0);
    ok &= (run("/bin/blocks.hxe") == 0);
    ok &= (run("/bin/lspci.hxe") == 0);
    ok &= (run("/bin/lsblk.hxe") == 0);
    if (ok) {
        print("[PASS] expanded coreutils\n");
    } else {
        print("[FAIL] expanded coreutils\n");
    }

    /* Prompt 5: storage user programs. */
    int sok = 1;
    sok &= (run("/tests/storage_test.hxe") == 0);
    sok &= (run("/tests/fs_test.hxe") == 0);
    sok &= (run("/tests/disk_test.hxe") == 0);
    sok &= (run("/tests/cache_test.hxe") == 0);
    if (sok) {
        print("[PASS] storage user programs\n");
    } else {
        print("[FAIL] storage user programs\n");
    }

    /* Shells: scripted, then interactive. */
    run("/bin/shell.hxe");
    char *const a_shell_i[] = { "/bin/shell.hxe", "-i", 0 };
    run_argv(a_shell_i);

    cat_file("/etc/motd.txt");
    print("[init] storage + device expansion complete\n");
    return 0;
}
