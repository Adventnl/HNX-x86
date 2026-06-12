/* Exception trap frame + dispatcher. */
#ifndef MYOS_X86_EXCEPTIONS_H
#define MYOS_X86_EXCEPTIONS_H

#include "types.h"

/* Built by the assembly stubs in isr.S (see that file for the exact push
 * order). `rdi` points at the lowest field (`vector`) on dispatch. */
struct x86_trap_frame {
    uint64_t vector;
    uint64_t error_code;

    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;

    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

void exceptions_init(void);
void x86_exception_dispatch(struct x86_trap_frame *frame);
const char *x86_exception_name(uint64_t vector);

void page_fault_dump(struct x86_trap_frame *frame);

#define VEC_PAGE_FAULT 14

#endif /* MYOS_X86_EXCEPTIONS_H */
