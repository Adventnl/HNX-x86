/* MyOS user C runtime entry.
 *
 * The kernel enters ring 3 at _start with the stack laid out SysV-style:
 *   [rsp+0]  = argc
 *   [rsp+8]  = argv[0]
 *   ...      = argv[argc-1], NULL, then the argument strings
 * The initial RSP is 16-byte aligned. We pass argc/argv to main, then hand
 * main's return value to the exit syscall (which never returns). */
#include "syscall_numbers.h"

    .text
    .global _start
_start:
    xorq %rbp, %rbp           /* terminate the frame chain */
    movq (%rsp), %rdi         /* argc */
    leaq 8(%rsp), %rsi        /* argv */
    call main                 /* int main(int argc, char **argv) -> eax */
    movl %eax, %edi           /* exit code = main's return value */
    movq $SYS_EXIT, %rax
    int  $0x80                /* exit — does not return */
1:  jmp 1b

    .section .note.GNU-stack, "", @progbits
