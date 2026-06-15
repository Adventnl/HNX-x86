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
#define SYS_MKDIR    17
#define SYS_UNLINK   18
#define SYS_STAT     19
#define SYS_MOUNT_INFO 20
#define SYS_DEVICES  21
#define SYS_BLOCKS   22
/* Prompt 6: hardware / USB / input introspection. */
#define SYS_USB_DEVICES 23
#define SYS_HW_INFO     24
#define SYS_INTERRUPTS  25
#define SYS_INPUT_POLL  26
#define SYS_MOUSE_POLL  27
#define SYS_MSI_INFO    28

/* Production Overhaul Phase 1 (Work Unit B): process / credentials / time /
 * fd / memory expansion. */
#define SYS_GETPPID       29
#define SYS_GETTID        30
#define SYS_GETUID        31
#define SYS_SETUID        32
#define SYS_GETGID        33
#define SYS_SETGID        34
#define SYS_GETPRIORITY   35
#define SYS_SETPRIORITY   36
#define SYS_BRK           37
#define SYS_SBRK          38
#define SYS_MMAP          39
#define SYS_MUNMAP        40
#define SYS_DUP           41
#define SYS_DUP2          42
#define SYS_PIPE          43
#define SYS_FCNTL         44
#define SYS_IOCTL         45
#define SYS_WAITPID       46
#define SYS_KILL          47
#define SYS_GETTIMEOFDAY  48
#define SYS_CLOCK_GETTIME 49
#define SYS_NANOSLEEP     50
#define SYS_GETPGID       51
#define SYS_SETPGID       52
#define SYS_GETSID        53
#define SYS_SETSID        54
#define SYS_ENV_GET       55
#define SYS_ENV_SET       56

#define SYS_MAX_NR   57        /* one past the highest valid number */

/* waitpid options. */
#define WNOHANG   0x1
#define WUNTRACED 0x2

/* fcntl commands. */
#define F_DUPFD  0
#define F_GETFD  1
#define F_SETFD  2
#define F_GETFL  3
#define F_SETFL  4
#define FD_CLOEXEC 1

/* mmap protection / flags (foundation). */
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED     0x10

/* clock ids. */
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

/* lseek whence. */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* open flags (small subset). */
#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_DIRECTORY 0x0010
#define O_CREAT  0x0040
#define O_TRUNC  0x0200

/* errno values (returned negated). A subset of the POSIX numbers; MyOS does not
 * claim POSIX compatibility, the names just ease future expansion. */
#define SYS_EPERM         1
#define SYS_ENOENT        2
#define SYS_ESRCH         3
#define SYS_EIO           5    /* low-level I/O error */
#define SYS_EEXIST       17    /* file already exists */
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
