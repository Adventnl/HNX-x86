/* storage_test: exercise the Prompt 5 storage syscalls from ring 3 — namespace
 * mutation on /disk plus the introspection calls. Prints "[PASS] storage user
 * programs" on success. */
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
    print("[test] storage_test start\n");

    /* mkdir + create + write + read round-trip on the persistent disk. */
    mkdir("/disk/ut");
    int fd = open("/disk/ut/file.txt", O_CREAT | O_WRONLY | O_TRUNC);
    check(fd >= 0, "open O_CREAT on /disk");
    const char *msg = "ring3 storage write";
    check(write(fd, msg, strlen(msg)) == (long)strlen(msg), "write /disk file");
    close(fd);

    char buf[64];
    memset(buf, 0, sizeof(buf));
    fd = open("/disk/ut/file.txt", O_RDONLY);
    check(fd >= 0, "reopen /disk file");
    long r = read(fd, buf, sizeof(buf));
    close(fd);
    check(r == (long)strlen(msg) && memcmp(buf, msg, strlen(msg)) == 0, "read back /disk file");

    /* stat the created file. */
    struct sys_stat st;
    check(stat("/disk/ut/file.txt", &st) == 0 && st.size == strlen(msg), "stat /disk file");

    /* introspection syscalls return sane counts. */
    struct sys_mount_entry m[8];
    check(mounts(m, 8) >= 2, "mounts >= 2 (/, /dev)");
    struct sys_device_entry d[32];
    check(devices(d, 32) >= 1, "devices >= 1");
    struct sys_block_entry b[16];
    check(blocks(b, 16) >= 1, "blocks >= 1 (disk0)");

    if (failures == 0) {
        print("[PASS] storage user programs\n");
        return 0;
    }
    print("[FAIL] storage_test\n");
    return 1;
}
