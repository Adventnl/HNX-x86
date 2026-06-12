/* Hardware IRQ subsystem (separate from CPU exceptions). */
#ifndef MYOS_X86_IRQ_H
#define MYOS_X86_IRQ_H

#include "types.h"

#define IRQ_BASE_VECTOR       0x20    /* legacy IRQ 0-15 -> vectors 0x20-0x2F */
#define LAPIC_TIMER_VECTOR    0x30
#define LAPIC_SPURIOUS_VECTOR 0xF0

/* Same layout as struct x86_trap_frame; built by irq_stubs.S. */
struct irq_context {
    uint64_t vector;
    uint64_t error_code;          /* always 0 for IRQs */

    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;

    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

typedef void (*irq_handler_t)(uint8_t vector, void *context);

void irq_init(void);
void irq_register_handler(uint8_t vector, irq_handler_t handler, void *context);
void irq_unregister_handler(uint8_t vector);
void irq_dispatch(struct irq_context *context);

void irq_enable(void);
void irq_disable(void);
uint64_t irq_save_flags_and_disable(void);
void irq_restore_flags(uint64_t flags);

uint64_t irq_count_for_vector(uint8_t vector);
void irq_dump_counts(void);

#endif /* MYOS_X86_IRQ_H */
