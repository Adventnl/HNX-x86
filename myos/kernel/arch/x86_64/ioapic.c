/* Minimal I/O APIC driver: enough to route the PS/2 keyboard IRQ. */
#include "ioapic.h"
#include "madt.h"
#include "vmm.h"
#include "memory_layout.h"
#include "log.h"

#define IOAPIC_REG_SELECT 0x00
#define IOAPIC_REG_WIN    0x10
#define IOAPIC_REDTBL(n)  (0x10 + 2u * (n))

#define REDIR_MASKED   (1u << 16)
#define REDIR_LEVEL    (1u << 15)
#define REDIR_ACTIVELO (1u << 13)
#define REDIR_LOGICAL  (1u << 11)

static volatile uint32_t *g_ioapic;      /* MMIO base (identity mapped CD/WT) */
static uint32_t g_gsi_base;
static int g_available;

static uint32_t ioapic_read(uint32_t reg) {
    g_ioapic[IOAPIC_REG_SELECT / 4] = reg;
    return g_ioapic[IOAPIC_REG_WIN / 4];
}

static void ioapic_write(uint32_t reg, uint32_t value) {
    g_ioapic[IOAPIC_REG_SELECT / 4] = reg;
    g_ioapic[IOAPIC_REG_WIN / 4] = value;
}

static uint32_t ioapic_max_redir(void) {
    return ((ioapic_read(0x01) >> 16) & 0xFF) + 1;   /* IOAPICVER[23:16]+1 */
}

int ioapic_init(void) {
    const struct madt_info *mi = madt_get_info();
    if (!mi || mi->io_apic_physical_base == 0) {
        g_available = 0;
        return -1;
    }
    uint64_t base = mi->io_apic_physical_base;
    if (vmm_map_mmio_2m(base & ~LARGE_PAGE_MASK) != 0) {
        return -1;
    }
    g_ioapic = (volatile uint32_t *)(uintptr_t)base;
    g_gsi_base = mi->global_system_interrupt_base;
    g_available = 1;

    uint32_t n = ioapic_max_redir();
    for (uint32_t i = 0; i < n; i++) {
        ioapic_write(IOAPIC_REDTBL(i), REDIR_MASKED);
        ioapic_write(IOAPIC_REDTBL(i) + 1, 0);
    }
    return 0;
}

int ioapic_available(void) {
    return g_available;
}

void ioapic_set_mask(uint32_t gsi, int masked) {
    if (!g_available || gsi < g_gsi_base) {
        return;
    }
    uint32_t idx = gsi - g_gsi_base;
    uint32_t low = ioapic_read(IOAPIC_REDTBL(idx));
    if (masked) {
        low |= REDIR_MASKED;
    } else {
        low &= ~REDIR_MASKED;
    }
    ioapic_write(IOAPIC_REDTBL(idx), low);
}

int ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t lapic_id) {
    if (!g_available) {
        return -1;
    }
    const struct madt_info *mi = madt_get_info();
    uint32_t gsi = irq;
    uint32_t flags = 0;
    /* The MADT only records the IRQ0 override here; honor it if it matches. */
    if (mi && mi->has_legacy_irq0_override && irq == 0) {
        gsi = mi->irq0_gsi;
        if ((mi->irq0_flags & 0x3) == 0x3) flags |= REDIR_ACTIVELO;
        if ((mi->irq0_flags & 0xC) == 0xC) flags |= REDIR_LEVEL;
    }
    if (gsi < g_gsi_base) {
        return -1;
    }
    uint32_t idx = gsi - g_gsi_base;
    ioapic_write(IOAPIC_REDTBL(idx) + 1, (uint32_t)lapic_id << 24);
    ioapic_write(IOAPIC_REDTBL(idx), (uint32_t)vector | flags);   /* unmasked */
    return 0;
}
