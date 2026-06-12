/* Exception assembly stubs for vectors 0-31.
 *
 * Each stub builds a `struct x86_trap_frame` on the stack and calls
 * x86_exception_dispatch(frame). The struct (low address -> high address) is:
 *
 *   vector, error_code,
 *   r15,r14,r13,r12,r11,r10,r9,r8, rbp,rdi,rsi,rdx,rcx,rbx,rax,
 *   rip, cs, rflags, rsp, ss      (the last five pushed by the CPU)
 *
 * GPRs sit between (vector,error_code) and the CPU frame, so the GPRs are
 * pushed first (rax adjacent to rip) and then error_code and vector last.
 * For vectors with a hardware error code, the CPU's error code initially sits
 * where rax must go; the xchg trick relocates it.
 */
    .text

    .macro PUSH_GPRS_NOERR
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    .endm

/* No hardware error code: push a 0 placeholder, then the vector. */
    .macro ISR_NOERR num
    .global isr\num
isr\num:
    PUSH_GPRS_NOERR
    pushq $0
    pushq $\num
    jmp   isr_common
    .endm

/* Hardware error code already on stack (just below rip). Convert that slot
 * into the saved-rax slot and stash the real error code in %rax. */
    .macro ISR_ERR num
    .global isr\num
isr\num:
    xchg  %rax, (%rsp)      /* %rax = error_code; [rsp] = original rax */
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    pushq %rax             /* error_code */
    pushq $\num            /* vector */
    jmp   isr_common
    .endm

isr_common:
    cld
    movq %rsp, %rdi        /* frame pointer -> first arg */
    call x86_exception_dispatch
    /* x86_exception_dispatch panics for Prompt 2; restore path kept correct. */
    addq $16, %rsp         /* drop vector + error_code */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    iretq

    /* Vectors 0-31. Error-code vectors: 8,10,11,12,13,14,17,21,29,30. */
    ISR_NOERR 0
    ISR_NOERR 1
    ISR_NOERR 2
    ISR_NOERR 3
    ISR_NOERR 4
    ISR_NOERR 5
    ISR_NOERR 6
    ISR_NOERR 7
    ISR_ERR   8
    ISR_NOERR 9
    ISR_ERR   10
    ISR_ERR   11
    ISR_ERR   12
    ISR_ERR   13
    ISR_ERR   14
    ISR_NOERR 15
    ISR_NOERR 16
    ISR_ERR   17
    ISR_NOERR 18
    ISR_NOERR 19
    ISR_NOERR 20
    ISR_ERR   21
    ISR_NOERR 22
    ISR_NOERR 23
    ISR_NOERR 24
    ISR_NOERR 25
    ISR_NOERR 26
    ISR_NOERR 27
    ISR_NOERR 28
    ISR_ERR   29
    ISR_ERR   30
    ISR_NOERR 31

    /* Table of stub addresses for idt_init(). */
    .section .rodata
    .align 8
    .global isr_stub_table
isr_stub_table:
    .quad isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7
    .quad isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15
    .quad isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    .quad isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31

    .section .note.GNU-stack, "", @progbits
