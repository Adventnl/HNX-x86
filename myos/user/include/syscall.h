/* Raw int 0x80 system-call entry + the shared ABI (numbers, errno, structs).
 * The numbers/errnos and structs come from the kernel headers (-Ikernel/user) so
 * the two sides can never disagree. */
#ifndef MYOS_USER_SYSCALL_H
#define MYOS_USER_SYSCALL_H

#include "types.h"
#include "syscall_numbers.h"
#include "syscall_abi.h"

/* number in rax; args in rdi/rsi/rdx; result in rax (negative = -errno). */
long __syscall(long number, long a0, long a1, long a2);

#endif /* MYOS_USER_SYSCALL_H */
