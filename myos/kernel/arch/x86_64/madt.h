/* ACPI MADT (APIC table) parser. */
#ifndef MYOS_X86_MADT_H
#define MYOS_X86_MADT_H

#include "types.h"

struct madt_info {
    uint64_t local_apic_physical_base;
    uint32_t local_apic_count;

    uint64_t io_apic_physical_base;
    uint32_t io_apic_id;
    uint32_t global_system_interrupt_base;

    uint8_t  has_legacy_irq0_override;
    uint32_t irq0_gsi;
    uint16_t irq0_flags;
};

/* Parse RSDP -> XSDT (preferred) / RSDT -> MADT. Returns 0 on success. */
int madt_init(uint64_t rsdp_address);
const struct madt_info *madt_get_info(void);
void madt_dump_info(void);

#endif /* MYOS_X86_MADT_H */
