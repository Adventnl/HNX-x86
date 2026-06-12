/* Interrupt Descriptor Table setup. */
#include "idt.h"
#include "gdt.h"
#include "cpu.h"
#include "tss.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;        /* bits 0-2 = IST index */
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES] __attribute__((aligned(16)));
static struct x86_descriptor_ptr idtr;

/* Stub table from isr.S. */
extern void *isr_stub_table[32];

void idt_set_gate(uint8_t vector, void *handler, uint16_t selector, uint8_t flags) {
    uint64_t addr = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt[vector].selector    = selector;
    idt[vector].ist         = 0;
    idt[vector].type_attr   = flags;
    idt[vector].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vector].zero        = 0;
}

void idt_set_ist(uint8_t vector, uint8_t ist) {
    idt[vector].ist = ist & 0x7;
}

void idt_init(void) {
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt[i] = (struct idt_entry){0};
    }

    /* Install exception vectors 0-31 as ring-0 64-bit interrupt gates. */
    for (int i = 0; i < 32; i++) {
        idt_set_gate((uint8_t)i, isr_stub_table[i],
                     GDT_KERNEL_CODE, IDT_FLAG_INTERRUPT_GATE);
    }

    /* Critical faults run on dedicated IST stacks. */
    idt_set_ist(8, IST_DOUBLE_FAULT);
    idt_set_ist(14, IST_PAGE_FAULT);

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)(uintptr_t)&idt;
    x86_lidt(&idtr);
}
