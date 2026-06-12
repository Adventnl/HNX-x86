/*
 * Kernel assembly entry point.
 *
 * The bootloader jumps here (System V ABI) with:
 *   RDI = struct boot_info *
 *
 * Responsibilities:
 *   - set up a known-good stack
 *   - align the stack to 16 bytes (SysV requirement at a call site)
 *   - call kernel_main(boot_info)
 *   - halt forever if kernel_main ever returns
 */
    .section .text
    .global kernel_entry
    .type kernel_entry, @function
kernel_entry:
    cli                                 /* no interrupts: we have no IDT yet */
    cld

    leaq    kernel_stack_top(%rip), %rsp    /* RSP -> top of static stack    */
    andq    $-16, %rsp                      /* 16-byte align                 */
    xorq    %rbp, %rbp                       /* terminate stack-frame chain  */

    /* RDI already holds boot_info (preserved from the bootloader). */
    call    kernel_main

.Lhang:
    cli
    hlt
    jmp     .Lhang

    .size kernel_entry, . - kernel_entry

/* ---- Static kernel stack (16 KiB) ---------------------------------------- */
    .section .bss
    .align 16
    .global kernel_stack
kernel_stack:
    .skip 16384
    .global kernel_stack_top
kernel_stack_top:

/* Mark the stack as non-executable; silence GNU-stack warnings. */
    .section .note.GNU-stack, "", @progbits
