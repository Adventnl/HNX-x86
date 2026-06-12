/* User-side system-call wrappers (int 0x80). The ABI numbers come from the
 * kernel's syscall_numbers.h (shared via -Ikernel/user) so the two never drift. */
#ifndef MYOS_USER_SYSCALL_H
#define MYOS_USER_SYSCALL_H

#include "start.h"
#include "syscall_numbers.h"

/* Raw call: rax=number, args in rdi/rsi/rdx; returns rax (negative = error). */
long usys(long number, long a0, long a1, long a2);

long    sys_write(int fd, const void *buf, unsigned long len);
long    sys_read(int fd, void *buf, unsigned long len);
void    sys_exit(int code) __attribute__((noreturn));
long    sys_getpid(void);
long    sys_yield(void);
long    sys_sleep(unsigned long milliseconds);

#endif /* MYOS_USER_SYSCALL_H */
