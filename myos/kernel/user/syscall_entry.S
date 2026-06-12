/* int 0x80 system-call assembly entry.
 *
 * On entry (ring 3 -> ring 0 via a DPL-3 interrupt gate) the CPU has switched
 * to the current thread's kernel stack (TSS.RSP0) and pushed SS, RSP, RFLAGS,
 * CS, RIP. We push all GPRs to complete a `struct syscall_frame`, call the C
 * dispatcher, write its return value into the saved RAX slot, restore the GPRs
 * and iretq back to ring 3.
 *
 * The gate is an interrupt gate, so IF is clear for the duration of the call
 * (no nesting); the user RFLAGS (IF=1) is restored by iretq. The frame layout
 * matches struct syscall_frame: r15 at the lowest address, rax 14 slots up. */
    .text
    .global syscall_entry
syscall_entry:
    cld
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

    movq %rsp, %rdi            /* struct syscall_frame * */
    call syscall_dispatch
    movq %rax, 14*8(%rsp)      /* overwrite saved rax with the return value */

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

    .section .note.GNU-stack, "", @progbits
