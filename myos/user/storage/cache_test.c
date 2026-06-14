/* cache_test: re-reading the same file should be served quickly from the block
 * cache. We can't see cache stats from ring 3, so we verify read correctness +
 * stability across repeated reads (which the cache must keep coherent). */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"

int main(void) {
    print("[test] cache_test start\n");
    int fd = open("/disk/cachetest", O_CREAT | O_WRONLY | O_TRUNC);
    if (fd < 0) {
        print("[FAIL] cache_test (create)\n");
        return 1;
    }
    const char *s = "cache-coherency-check";
    write(fd, s, strlen(s));
    close(fd);

    int ok = 1;
    for (int pass = 0; pass < 3; pass++) {
        char buf[64];
        memset(buf, 0, sizeof(buf));
        fd = open("/disk/cachetest", O_RDONLY);
        long r = read(fd, buf, sizeof(buf));
        close(fd);
        if (r != (long)strlen(s) || memcmp(buf, s, strlen(s)) != 0) {
            ok = 0;
        }
    }
    unlink("/disk/cachetest");

    if (ok) {
        print("[PASS] cache_test\n");
        return 0;
    }
    print("[FAIL] cache_test\n");
    return 1;
}
