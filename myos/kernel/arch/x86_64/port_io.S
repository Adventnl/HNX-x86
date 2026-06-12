/* Port I/O primitives. */
    .text

/* uint8_t x86_inb(uint16_t port)   port in DI */
    .global x86_inb
x86_inb:
    movw %di, %dx
    xorq %rax, %rax
    inb  %dx, %al
    ret

/* void x86_outb(uint16_t port, uint8_t value)   port in DI, value in SIL */
    .global x86_outb
x86_outb:
    movw %di, %dx
    movb %sil, %al
    outb %al, %dx
    ret

    .section .note.GNU-stack, "", @progbits
