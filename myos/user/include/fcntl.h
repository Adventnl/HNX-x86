/* File-control: open() and the O_* flags (shared from syscall_numbers.h). */
#ifndef MYOS_USER_FCNTL_H
#define MYOS_USER_FCNTL_H

#include "syscall_numbers.h"

int open(const char *path, int flags);

#endif /* MYOS_USER_FCNTL_H */
