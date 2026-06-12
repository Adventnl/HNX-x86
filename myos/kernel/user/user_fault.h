/* Ring-3 fault isolation: a CPL-3 CPU exception terminates the offending user
 * task instead of panicking the kernel. */
#ifndef MYOS_USER_FAULT_H
#define MYOS_USER_FAULT_H

#include "types.h"

struct x86_trap_frame;

/* Handle a fault taken while CS.RPL == 3. Prints a diagnostic, marks the
 * current user task faulted, and schedules away. Never returns. */
void user_fault_handle(struct x86_trap_frame *frame) __attribute__((noreturn));

#endif /* MYOS_USER_FAULT_H */
