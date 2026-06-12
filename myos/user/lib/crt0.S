/* MyOS user C runtime entry.
 *
 * The kernel enters ring 3 at _start with the user stack already set up. We
 * terminate the frame chain, call main(), then pass main's return value to the
 * exit syscall (which never returns). */
#include "syscall_numbers.h"

    .text
    .global _start
_start:
    xorq %rbp, %rbp           /* terminate the stack-frame chain */
    call main                 /* int main(void) -> eax           */
    movl %eax, %edi           /* exit code = main's return value */
    movq $SYS_EXIT, %rax
    int  $0x80                /* exit syscall — does not return  */
1:  jmp 1b                    /* belt-and-braces if exit returns */

    .section .note.GNU-stack, "", @progbits
