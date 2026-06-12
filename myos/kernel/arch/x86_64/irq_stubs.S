/* Hardware-IRQ assembly stubs (separate from the exception stubs in isr.S).
 *
 * Covers vectors 0x20-0x2F (legacy IRQ range), 0x30 (LAPIC timer) and 0xF0
 * (LAPIC spurious). Hardware IRQs never push an error code, so every stub
 * pushes a fake 0. The resulting stack matches struct irq_context exactly
 * (vector at the lowest address; SS:RSP always pushed by the CPU in long
 * mode, same as the exception path).
 *
 * Unlike exceptions, the dispatch here RETURNS — the restore path below is
 * the hot path, executed on every tick. irq_dispatch may also context-switch
 * away (timer preemption); in that case this frame stays intact on the
 * preempted thread's stack and is resumed exactly here later.
 */
    .text

    .macro IRQ_STUB num
    .global irq_stub_\num
irq_stub_\num:
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
    pushq $0               /* fake error code */
    pushq $\num            /* vector */
    jmp   irq_common
    .endm

irq_common:
    cld
    movq %rsp, %rdi        /* struct irq_context * */
    call irq_dispatch
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

    IRQ_STUB 0x20
    IRQ_STUB 0x21
    IRQ_STUB 0x22
    IRQ_STUB 0x23
    IRQ_STUB 0x24
    IRQ_STUB 0x25
    IRQ_STUB 0x26
    IRQ_STUB 0x27
    IRQ_STUB 0x28
    IRQ_STUB 0x29
    IRQ_STUB 0x2A
    IRQ_STUB 0x2B
    IRQ_STUB 0x2C
    IRQ_STUB 0x2D
    IRQ_STUB 0x2E
    IRQ_STUB 0x2F
    IRQ_STUB 0x30
    IRQ_STUB 0xF0

    /* Index order: 0x20..0x2F (0-15), 0x30 (16), 0xF0 (17). */
    .section .rodata
    .align 8
    .global irq_stub_table
irq_stub_table:
    .quad irq_stub_0x20, irq_stub_0x21, irq_stub_0x22, irq_stub_0x23
    .quad irq_stub_0x24, irq_stub_0x25, irq_stub_0x26, irq_stub_0x27
    .quad irq_stub_0x28, irq_stub_0x29, irq_stub_0x2A, irq_stub_0x2B
    .quad irq_stub_0x2C, irq_stub_0x2D, irq_stub_0x2E, irq_stub_0x2F
    .quad irq_stub_0x30, irq_stub_0xF0

    .section .note.GNU-stack, "", @progbits
