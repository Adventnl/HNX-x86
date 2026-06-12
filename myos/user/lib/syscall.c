/* int 0x80 system-call wrappers. The kernel preserves every GPR except rax, so
 * only rax is an output; "memory" prevents the compiler from caching across the
 * call. */
#include "syscall.h"

long usys(long number, long a0, long a1, long a2) {
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(number), "D"(a0), "S"(a1), "d"(a2)
                     : "memory");
    return ret;
}

long sys_write(int fd, const void *buf, unsigned long len) {
    return usys(SYS_WRITE, fd, (long)buf, (long)len);
}

long sys_read(int fd, void *buf, unsigned long len) {
    return usys(SYS_READ, fd, (long)buf, (long)len);
}

void sys_exit(int code) {
    usys(SYS_EXIT, code, 0, 0);
    for (;;) {           /* exit never returns; satisfy noreturn */
    }
}

long sys_getpid(void) {
    return usys(SYS_GETPID, 0, 0, 0);
}

long sys_yield(void) {
    return usys(SYS_YIELD, 0, 0, 0);
}

long sys_sleep(unsigned long milliseconds) {
    return usys(SYS_SLEEP, (long)milliseconds, 0, 0);
}
