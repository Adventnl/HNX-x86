/* Interrupt Descriptor Table (x86-64). */
#ifndef MYOS_X86_IDT_H
#define MYOS_X86_IDT_H

#include "types.h"

#define IDT_ENTRIES 256

/* type_attr for a present, ring-0, 64-bit interrupt gate. */
#define IDT_FLAG_INTERRUPT_GATE 0x8E

void idt_init(void);
void idt_set_gate(uint8_t vector, void *handler, uint16_t selector, uint8_t flags);

/* Set the IST index (0..7) for a vector; 0 = use the current stack. */
void idt_set_ist(uint8_t vector, uint8_t ist);

#endif /* MYOS_X86_IDT_H */
