/* vfs_test: exercise path resolution, file read, directory listing and stat-ish
 * behaviour. Prints "[PASS] vfs_test" on success (greped by verify-vfs). */
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
    print("[test] vfs_test start\n");

    /* open + read a real file. */
    int fd = open("/etc/banner.txt", O_RDONLY);
    check(fd >= 0, "open /etc/banner.txt");
    char buf[64];
    long r = read(fd, buf, sizeof(buf));
    check(r > 0, "read /etc/banner.txt");
    close(fd);

    /* lseek to end then back. */
    fd = open("/etc/banner.txt", O_RDONLY);
    long end = lseek(fd, 0, SEEK_END);
    check(end > 0, "lseek END > 0");
    check(lseek(fd, 0, SEEK_SET) == 0, "lseek SET 0");
    close(fd);

    /* missing path fails. */
    check(open("/no/such/file", O_RDONLY) < 0, "open missing fails");

    /* directory listing of / contains bin + etc. */
    int d = open("/", O_DIRECTORY);
    check(d >= 0, "open / as directory");
    struct sys_dirent de;
    int found_bin = 0, found_etc = 0, count = 0;
    while (readdir(d, &de) == 1 && count < 64) {
        if (strcmp(de.name, "bin") == 0) found_bin = 1;
        if (strcmp(de.name, "etc") == 0) found_etc = 1;
        count++;
    }
    close(d);
    check(found_bin && found_etc, "readdir / has bin + etc");

    /* /dev/zero behaves. */
    int z = open("/dev/zero", O_RDONLY);
    char zb[4];
    memset(zb, 1, sizeof(zb));
    check(read(z, zb, sizeof(zb)) == (long)sizeof(zb) && zb[0] == 0, "/dev/zero reads zero");
    close(z);

    if (failures == 0) {
        print("[PASS] vfs_test\n");
        return 0;
    }
    print("[FAIL] vfs_test\n");
    return 1;
}
