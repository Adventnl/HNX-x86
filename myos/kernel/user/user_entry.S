/* Ring-3 entry.
 *
 * void user_enter_ring3(uint64_t user_rip [rdi],
 *                       uint64_t user_rsp [rsi],
 *                       uint64_t user_cr3 [rdx]);
 *
 * Loads the user CR3, then builds an iretq frame that drops to CPL 3:
 *   SS     = 0x1B  (user data, RPL 3)
 *   RSP    = user_rsp
 *   RFLAGS = IF set (0x202: IF | reserved-1)
 *   CS     = 0x23  (user code, RPL 3)
 *   RIP    = user_rip
 * Clears the GPRs first so no kernel register state leaks into ring 3. */
    .text
    .global user_enter_ring3
user_enter_ring3:
    cli
    movq %rdx, %cr3            /* activate the user address space */

    movq %rdi, %rcx           /* stash entry RIP                 */
    movq %rsi, %rbx           /* stash user RSP                  */

    pushq $0x1B               /* SS                              */
    pushq %rbx                /* RSP                             */
    pushq $0x202              /* RFLAGS (IF=1)                   */
    pushq $0x23               /* CS                              */
    pushq %rcx                /* RIP                             */

    xorq %rax, %rax
    xorq %rbx, %rbx
    xorq %rcx, %rcx
    xorq %rdx, %rdx
    xorq %rsi, %rsi
    xorq %rdi, %rdi
    xorq %rbp, %rbp
    xorq %r8,  %r8
    xorq %r9,  %r9
    xorq %r10, %r10
    xorq %r11, %r11
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15
    iretq

    .section .note.GNU-stack, "", @progbits
