/* Raw int 0x80 wrapper. The kernel preserves every GPR except rax, so rax is the
 * only output; "memory" stops the compiler caching across the trap. */
#include "syscall.h"

long __syscall(long number, long a0, long a1, long a2) {
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(number), "D"(a0), "S"(a1), "d"(a2)
                     : "memory");
    return ret;
}
