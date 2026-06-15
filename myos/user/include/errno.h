/* errno names mapped to the SYS_E* numbers shared with the kernel. MyOS does not
 * maintain a global `errno` variable (syscalls return -errno directly); these
 * names exist so programs can compare a negated syscall result symbolically and
 * strerror() can render it. */
#ifndef MYOS_USER_ERRNO_H
#define MYOS_USER_ERRNO_H

#include "syscall_numbers.h"

#define EPERM         SYS_EPERM
#define ENOENT        SYS_ENOENT
#define ESRCH         SYS_ESRCH
#define EIO           SYS_EIO
#define EBADF         SYS_EBADF
#define ECHILD        SYS_ECHILD
#define ENOMEM        SYS_ENOMEM
#define EFAULT        SYS_EFAULT
#define EEXIST        SYS_EEXIST
#define ENOTDIR       SYS_ENOTDIR
#define EISDIR        SYS_EISDIR
#define EINVAL        SYS_EINVAL
#define EMFILE        SYS_EMFILE
#define ERANGE        SYS_ERANGE
#define ENAMETOOLONG  SYS_ENAMETOOLONG
#define ENOSYS        SYS_ENOSYS
#define ENOEXEC       SYS_ENOEXEC

/* Render an errno number (positive) into a short human string. */
const char *strerror(int err);

#endif /* MYOS_USER_ERRNO_H */
