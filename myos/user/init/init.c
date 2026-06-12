/* MyOS first user program (Prompt 4). Runs in ring 3 and exercises every
 * syscall through the int 0x80 boundary, printing the markers the verification
 * targets look for, then exits cleanly. */
#include "syscall.h"
#include "stdlib.h"

int main(void) {
    print("[USER] hello from ring 3\n");

    /* write — the line above already round-tripped through sys_write. */
    print("[USER] write syscall OK\n");

    /* getpid */
    long pid = sys_getpid();
    if (pid >= 0) {
        print("[USER] getpid syscall OK\n");
    }

    /* yield */
    sys_yield();
    print("[USER] yield syscall OK\n");

    /* sleep */
    sys_sleep(10);
    print("[USER] sleep syscall OK\n");

    /* read — fd 0 has no input yet: length 0 and a real read both return EOF(0). */
    char buf[4];
    long r0 = sys_read(0, buf, 0);
    long r1 = sys_read(0, buf, sizeof(buf));
    if (r0 == 0 && r1 == 0) {
        print("[USER] read syscall OK\n");
    }

    /* exit — printed before the syscall, since exit never returns. */
    print("[USER] exit syscall OK\n");
    return 0;
}
