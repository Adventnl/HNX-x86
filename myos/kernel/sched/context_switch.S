/* Cooperative context switch (System V AMD64).
 *
 * void context_switch(uint64_t *old_rsp [rdi], uint64_t new_rsp [rsi]);
 *
 * Saves the callee-saved registers (rbp, rbx, r12-r15) on the current stack,
 * stores RSP into *old_rsp, switches to new_rsp, restores the same registers
 * and returns — landing either in the new thread's previous context_switch
 * call site or, for a brand-new thread, in thread_trampoline (whose address
 * thread_create_raw planted as the return slot of the initial frame).
 *
 * Caller-saved registers don't need saving: the C caller already assumes
 * they are clobbered by any call. A preempted thread's full GPR state lives
 * in its irq_stubs.S frame further up the same stack.
 *
 * Initial stack frame built by thread_create_raw (top -> down):
 *   [alignment pad = 0]
 *   [thread_trampoline]   <- ret target
 *   [rbp=0][rbx=0][r12=0][r13=0][r14=0][r15=0]   <- thread->rsp points here
 */
    .text
    .global context_switch
context_switch:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, (%rdi)       /* save current stack pointer  */
    movq %rsi, %rsp         /* switch to the new stack     */

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

    .section .note.GNU-stack, "", @progbits
