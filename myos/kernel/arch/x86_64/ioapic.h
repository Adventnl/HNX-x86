/* I/O APIC: routes legacy ISA IRQs (e.g. PS/2 keyboard IRQ1) to LAPIC vectors.
 * The 8259 PIC is masked, so without this no legacy IRQ reaches the CPU. */
#ifndef MYOS_X86_IOAPIC_H
#define MYOS_X86_IOAPIC_H

#include "types.h"

/* Map the I/O APIC MMIO window (from the MADT) and mask all entries. Returns 0
 * on success, -1 if no I/O APIC was found. */
int  ioapic_init(void);

/* Route ISA `irq` to `vector`, delivered to `lapic_id`, edge/active-high,
 * unmasked. Applies the MADT IRQ-source override for the GSI when present. */
int  ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t lapic_id);

/* Mask/unmask the redirection entry for a global system interrupt. */
void ioapic_set_mask(uint32_t gsi, int masked);

int  ioapic_available(void);

#endif /* MYOS_X86_IOAPIC_H */
