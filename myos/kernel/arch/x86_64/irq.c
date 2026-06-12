/* Hardware IRQ dispatcher.
 *
 * EOI policy: the LAPIC EOI is sent AFTER the handler returns but BEFORE any
 * preemption context switch. The scheduler tick handler only sets a
 * need-resched flag; irq_dispatch performs the actual switch (via
 * scheduler_irq_exit()) after EOI so the next timer interrupt is never
 * blocked by a thread that was switched away mid-handler. */
#include "irq.h"
#include "idt.h"
#include "gdt.h"
#include "apic.h"
#include "cpu.h"
#include "log.h"
#include "scheduler.h"

extern void *irq_stub_table[18];

static irq_handler_t g_handlers[256];
static void         *g_handler_ctx[256];
static uint64_t      g_counts[256];
static uint8_t       g_warned[256];

void irq_init(void) {
    for (int i = 0; i < 256; i++) {
        g_handlers[i] = NULL;
        g_handler_ctx[i] = NULL;
        g_counts[i] = 0;
        g_warned[i] = 0;
    }

    /* Install stubs as ring-0 interrupt gates (IF cleared on entry). */
    for (int i = 0; i < 16; i++) {
        idt_set_gate((uint8_t)(IRQ_BASE_VECTOR + i), irq_stub_table[i],
                     GDT_KERNEL_CODE, IDT_FLAG_INTERRUPT_GATE);
    }
    idt_set_gate(LAPIC_TIMER_VECTOR, irq_stub_table[16],
                 GDT_KERNEL_CODE, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(LAPIC_SPURIOUS_VECTOR, irq_stub_table[17],
                 GDT_KERNEL_CODE, IDT_FLAG_INTERRUPT_GATE);
}

void irq_register_handler(uint8_t vector, irq_handler_t handler, void *context) {
    uint64_t f = irq_save_flags_and_disable();
    g_handlers[vector] = handler;
    g_handler_ctx[vector] = context;
    irq_restore_flags(f);
}

void irq_unregister_handler(uint8_t vector) {
    uint64_t f = irq_save_flags_and_disable();
    g_handlers[vector] = NULL;
    g_handler_ctx[vector] = NULL;
    irq_restore_flags(f);
}

void irq_dispatch(struct irq_context *context) {
    uint8_t vector = (uint8_t)context->vector;
    g_counts[vector]++;

    /* Spurious interrupts get no EOI and no handler. */
    if (vector == LAPIC_SPURIOUS_VECTOR) {
        return;
    }

    irq_handler_t handler = g_handlers[vector];
    if (handler) {
        handler(vector, g_handler_ctx[vector]);
    } else if (!g_warned[vector]) {
        g_warned[vector] = 1;
        kernel_log_warn("unhandled IRQ vector:");
        kernel_log_hex64("    vector = ", vector);
    }

    /* Acknowledge before any preemption switch (see file header). */
    lapic_send_eoi();

    /* Timer preemption: switch away here, outside the handler, after EOI. */
    scheduler_irq_exit();
}

void irq_enable(void)  { x86_sti(); }
void irq_disable(void) { x86_cli(); }

uint64_t irq_save_flags_and_disable(void) {
    uint64_t flags = x86_read_rflags();
    x86_cli();
    return flags;
}

void irq_restore_flags(uint64_t flags) {
    if (flags & RFLAGS_IF) {
        x86_sti();
    }
}

uint64_t irq_count_for_vector(uint8_t vector) {
    return g_counts[vector];
}

void irq_dump_counts(void) {
    for (int v = 0; v < 256; v++) {
        if (g_counts[v]) {
            kernel_log_hex64("    irq vector ", (uint64_t)v);
            kernel_log_hex64("      count    ", g_counts[v]);
        }
    }
}
