/* System-call numbers + the int 0x80 vector. Shared by the kernel dispatcher
 * and the user runtime (kept self-contained so user code can include it). */
#ifndef MYOS_SYSCALL_NUMBERS_H
#define MYOS_SYSCALL_NUMBERS_H

#define SYSCALL_VECTOR 0x80

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_READ   2
#define SYS_SLEEP  3
#define SYS_GETPID 4
#define SYS_YIELD  5

/* Error returns (negative). Mirrors a small subset of POSIX errno values. */
#define SYS_EBADF   9     /* bad file descriptor   */
#define SYS_EFAULT  14    /* bad user pointer      */
#define SYS_ENOSYS  38    /* invalid syscall       */

#endif /* MYOS_SYSCALL_NUMBERS_H */
