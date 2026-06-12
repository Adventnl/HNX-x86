/* CPU halt helpers. */
    .text

    .global x86_halt
x86_halt:
    hlt
    ret

    .global x86_halt_forever
x86_halt_forever:
    cli
1:  hlt
    jmp 1b

    .section .note.GNU-stack, "", @progbits
