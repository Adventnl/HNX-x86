/* fs_test: HNXFS file/dir lifecycle from ring 3. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"

static int failures;
static void check(int cond, const char *name) {
    if (!cond) { failures++; printf("[FAIL] %s\n", name); }
}

int main(void) {
    print("[test] fs_test start\n");

    check(mkdir("/disk/fstest") == 0 || 1, "mkdir /disk/fstest");

    int fd = open("/disk/fstest/data", O_CREAT | O_WRONLY | O_TRUNC);
    check(fd >= 0, "create file");
    const char *s = "persistent-data-0123456789";
    check(write(fd, s, strlen(s)) == (long)strlen(s), "write file");
    close(fd);

    char buf[64];
    memset(buf, 0, sizeof(buf));
    fd = open("/disk/fstest/data", O_RDONLY);
    check(read(fd, buf, sizeof(buf)) == (long)strlen(s), "read file size");
    check(memcmp(buf, s, strlen(s)) == 0, "read file content");
    close(fd);

    /* directory listing contains our file. */
    int dfd = open("/disk/fstest", O_DIRECTORY);
    int found = 0;
    struct sys_dirent de;
    while (dfd >= 0 && readdir(dfd, &de) == 1) {
        if (strcmp(de.name, "data") == 0) {
            found = 1;
        }
    }
    if (dfd >= 0) close(dfd);
    check(found, "readdir finds file");

    check(unlink("/disk/fstest/data") == 0, "unlink file");

    if (failures == 0) {
        print("[PASS] fs_test\n");
        return 0;
    }
    print("[FAIL] fs_test\n");
    return 1;
}
