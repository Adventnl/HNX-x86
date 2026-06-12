/* Control / flag register access. */
    .text

    .global x86_read_cr0
x86_read_cr0:
    movq %cr0, %rax
    ret

    .global x86_read_cr2
x86_read_cr2:
    movq %cr2, %rax
    ret

    .global x86_read_cr3
x86_read_cr3:
    movq %cr3, %rax
    ret

    .global x86_read_cr4
x86_read_cr4:
    movq %cr4, %rax
    ret

    .global x86_write_cr0
x86_write_cr0:
    movq %rdi, %cr0
    ret

    .global x86_write_cr3
x86_write_cr3:
    movq %rdi, %cr3
    ret

    .global x86_write_cr4
x86_write_cr4:
    movq %rdi, %cr4
    ret

    .global x86_read_rflags
x86_read_rflags:
    pushfq
    popq %rax
    ret

    .section .note.GNU-stack, "", @progbits
