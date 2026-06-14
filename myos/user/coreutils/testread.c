/* testread: read a fixed amount from /dev/zero and verify it reads as zeroes,
 * exercising the open/read/close path. */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"

int main(void) {
    int fd = open("/dev/zero", O_RDONLY);
    if (fd < 0) {
        print("testread: cannot open /dev/zero\n");
        return 1;
    }
    char buf[32];
    memset(buf, 0xAB, sizeof(buf));
    long n = read(fd, buf, sizeof(buf));
    close(fd);

    int zeroed = (n == (long)sizeof(buf));
    for (long i = 0; i < n; i++) {
        if (buf[i] != 0) {
            zeroed = 0;
        }
    }
    printf("testread: read %ld bytes from /dev/zero, %s\n",
           n, zeroed ? "all zero" : "MISMATCH");
    return zeroed ? 0 : 1;
}
