/* Descriptor-table loads and segment reload. */
    .text

/* void x86_lgdt(void *gdtr) */
    .global x86_lgdt
x86_lgdt:
    lgdt (%rdi)
    ret

/* void x86_lidt(void *idtr) */
    .global x86_lidt
x86_lidt:
    lidt (%rdi)
    ret

/* void x86_ltr(uint16_t selector)   selector in DI */
    .global x86_ltr
x86_ltr:
    ltr %di
    ret

/* void gdt_reload_segments(void)
 * Reload the data segment registers and far-return to reload CS. */
    .global gdt_reload_segments
gdt_reload_segments:
    movw $0x10, %ax        /* GDT_KERNEL_DATA */
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    leaq 1f(%rip), %rax
    pushq $0x08            /* GDT_KERNEL_CODE */
    pushq %rax
    lretq
1:
    ret

    .section .note.GNU-stack, "", @progbits
