/* System-call numbers, the int 0x80 vector, and errno values. Shared verbatim
 * by the kernel dispatcher and the user runtime (-Ikernel/user) so the ABI can
 * never drift between the two sides. */
#ifndef MYOS_SYSCALL_NUMBERS_H
#define MYOS_SYSCALL_NUMBERS_H

#define SYSCALL_VECTOR 0x80

/* ABI: rax = number; args rdi, rsi, rdx, r10, r8, r9; result in rax (negative
 * values are -errno). */
#define SYS_EXIT     0
#define SYS_WRITE    1
#define SYS_READ     2
#define SYS_SLEEP    3
#define SYS_GETPID   4
#define SYS_YIELD    5
#define SYS_OPEN     6
#define SYS_CLOSE    7
#define SYS_LSEEK    8
#define SYS_READDIR  9
#define SYS_SPAWN    10
#define SYS_WAIT     11
#define SYS_GETCWD   12
#define SYS_CHDIR    13
#define SYS_UPTIME   14
#define SYS_MEMINFO  15
#define SYS_PS       16

#define SYS_MAX_NR   17        /* one past the highest valid number */

/* lseek whence. */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* open flags (small subset). */
#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_DIRECTORY 0x0010

/* errno values (returned negated). A subset of the POSIX numbers; MyOS does not
 * claim POSIX compatibility, the names just ease future expansion. */
#define SYS_EPERM         1
#define SYS_ENOENT        2
#define SYS_ESRCH         3
#define SYS_EBADF         9
#define SYS_ECHILD       10
#define SYS_ENOMEM       12
#define SYS_EFAULT       14
#define SYS_ENOTDIR      20
#define SYS_EISDIR       21
#define SYS_EINVAL       22
#define SYS_EMFILE       24
#define SYS_ERANGE       34
#define SYS_ENAMETOOLONG 36
#define SYS_ENOSYS       38    /* invalid / unimplemented syscall */
#define SYS_ENOEXEC       8    /* malformed executable */

#endif /* MYOS_SYSCALL_NUMBERS_H */
