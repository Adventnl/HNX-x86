/* Local APIC driver. */
#ifndef MYOS_X86_APIC_H
#define MYOS_X86_APIC_H

#include "types.h"

/* LAPIC register offsets (from the MMIO base). */
#define LAPIC_REG_ID            0x020
#define LAPIC_REG_VERSION       0x030
#define LAPIC_REG_TPR           0x080
#define LAPIC_REG_EOI           0x0B0
#define LAPIC_REG_SPURIOUS      0x0F0
#define LAPIC_REG_LVT_TIMER     0x320
#define LAPIC_REG_LVT_LINT0     0x350
#define LAPIC_REG_LVT_LINT1     0x360
#define LAPIC_REG_LVT_ERROR     0x370
#define LAPIC_REG_TIMER_INIT    0x380
#define LAPIC_REG_TIMER_CURRENT 0x390
#define LAPIC_REG_TIMER_DIVIDE  0x3E0

#define LAPIC_SPURIOUS_ENABLE   (1u << 8)
#define LAPIC_LVT_MASKED        (1u << 16)
#define LAPIC_LVT_TIMER_PERIODIC (1u << 17)
#define LAPIC_TIMER_DIVIDE_16   0x3

int      lapic_discover(void);          /* MSR + MADT cross-check; 0 on success */
uint64_t lapic_physical_base(void);
uint64_t lapic_virtual_base(void);      /* == physical (identity mapped, CD/WT) */
uint32_t lapic_read(uint32_t offset);
void     lapic_write(uint32_t offset, uint32_t value);
void     lapic_enable(void);
void     lapic_send_eoi(void);

#endif /* MYOS_X86_APIC_H */
