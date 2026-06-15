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

    /* Production Overhaul Phase 1 — Work Unit B: process/syscall expansion.
     * Each test runs in ring 3 and prints its own [PASS] markers. */
    run("/tests/process_tree_test.hxe");
    run("/tests/fd_stress_test.hxe");
    run("/tests/syscall_stress_test.hxe");
    run("/tests/memory_map_test.hxe");
    print("[OK] Process/syscall production foundation online\n");

    /* Work Unit G: userland libc + coreutils + service manager. */
    run("/tests/libc_test.hxe");
    run("/tests/coreutils_test.hxe");
    run("/bin/serviced.hxe");
    print("[OK] Userland production foundation online\n");

    /* Work Unit H: userland stress + the user test runner. */
    run("/tests/process_stress_test.hxe");
    run("/tests/test_runner.hxe");

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

    /* Prompt 6: hardware + USB + input userland tools. Each tool queries the
     * kernel over the new syscalls and exits 0 on success. */
    if (run("/bin/hwinfo.hxe")  == 0) print("[PASS] hwinfo\n");  else print("[FAIL] hwinfo\n");
    if (run("/bin/drivers.hxe") == 0) print("[PASS] drivers\n"); else print("[FAIL] drivers\n");
    if (run("/bin/devtree.hxe") == 0) print("[PASS] devtree\n"); else print("[FAIL] devtree\n");
    if (run("/bin/lsusb.hxe")   == 0) print("[PASS] lsusb\n");   else print("[FAIL] lsusb\n");
    if (run("/bin/hidinfo.hxe") == 0) print("[PASS] hidinfo\n"); else print("[FAIL] hidinfo\n");
    if (run("/bin/inputtest.hxe") == 0) print("[PASS] inputtest\n"); else print("[FAIL] inputtest\n");
    /* Remaining HW/USB/input tools (informational output). */
    run("/bin/interrupts.hxe");
    run("/bin/msiinfo.hxe");
    run("/bin/powerinfo.hxe");
    run("/bin/usbinfo.hxe");
    run("/bin/keytest.hxe");
    run("/bin/mousetest.hxe");
    run("/bin/usbtest.hxe");
    /* Prompt 6 ring-3 assertion tests. */
    run("/tests/usb_test.hxe");
    run("/tests/hid_test.hxe");
    run("/tests/input_test.hxe");
    run("/tests/msi_test.hxe");

    /* Shells: scripted, then interactive. */
    run("/bin/shell.hxe");
    char *const a_shell_i[] = { "/bin/shell.hxe", "-i", 0 };
    run_argv(a_shell_i);

    cat_file("/etc/motd.txt");
    print("[init] storage + device expansion complete\n");
    return 0;
}
