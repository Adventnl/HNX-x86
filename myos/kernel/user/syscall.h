/* int 0x80 system-call dispatch (Prompt 4 uses the software-interrupt path, not
 * syscall/sysret).
 *
 * User ABI:  rax = number, args in rdi, rsi, rdx, r10, r8, r9; result in rax;
 *            negative return values are errors. */
#ifndef MYOS_SYSCALL_H
#define MYOS_SYSCALL_H

#include "types.h"
#include "syscall_numbers.h"

/* Built by syscall_entry.S (same field order as struct irq_context). */
struct syscall_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;   /* pushed by the CPU on int 0x80 */
};

/* Assembly entry installed on IDT vector 0x80 (DPL 3). */
void syscall_entry(void);

/* Install the IDT gate and announce the dispatcher. */
void syscall_init(void);

/* C dispatcher: decode the frame, run the handler, return the result (placed
 * back into the user's rax by syscall_entry.S). Never panics on bad input. */
int64_t syscall_dispatch(struct syscall_frame *frame);

#endif /* MYOS_SYSCALL_H */
