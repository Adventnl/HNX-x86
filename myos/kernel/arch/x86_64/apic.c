/* Local APIC: discovery (MSR 0x1B + MADT cross-check), enable via the
 * spurious-interrupt vector register, EOI, and MMIO register access.
 *
 * The LAPIC window is identity-mapped; lapic_discover() re-maps its 2 MiB
 * page with cache-disable/write-through attributes via the VMM. */
#include "apic.h"
#include "cpu.h"
#include "madt.h"
#include "irq.h"
#include "vmm.h"
#include "memory_layout.h"

static uint64_t g_lapic_base;
static volatile uint32_t *g_lapic;

int lapic_discover(void) {
    uint64_t msr = x86_read_msr(MSR_APIC_BASE);
    uint64_t msr_base = msr & 0xFFFFF000ULL;

    const struct madt_info *mi = madt_get_info();
    uint64_t base = msr_base;
    if (base == 0 && mi) {
        base = mi->local_apic_physical_base;   /* fallback to ACPI */
    }
    if (base == 0) {
        return -1;
    }

    /* Map the containing 2 MiB page as MMIO (CD + WT). */
    if (vmm_map_mmio_2m(base & ~LARGE_PAGE_MASK) != 0) {
        return -1;
    }

    g_lapic_base = base;
    g_lapic = (volatile uint32_t *)(uintptr_t)base;
    return 0;
}

uint64_t lapic_physical_base(void) {
    return g_lapic_base;
}

uint64_t lapic_virtual_base(void) {
    return g_lapic_base;        /* identity mapped */
}

uint32_t lapic_read(uint32_t offset) {
    return g_lapic[offset / 4];
}

void lapic_write(uint32_t offset, uint32_t value) {
    g_lapic[offset / 4] = value;
}

void lapic_enable(void) {
    /* Global enable in the APIC base MSR (already set by firmware normally). */
    uint64_t msr = x86_read_msr(MSR_APIC_BASE);
    if (!(msr & (1ULL << 11))) {
        x86_write_msr(MSR_APIC_BASE, msr | (1ULL << 11));
    }

    /* Software enable + spurious vector. */
    lapic_write(LAPIC_REG_SPURIOUS, LAPIC_SPURIOUS_ENABLE | LAPIC_SPURIOUS_VECTOR);

    /* Accept all priorities; mask LVT lines we do not use yet. */
    lapic_write(LAPIC_REG_TPR, 0);
    lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_REG_LVT_ERROR, LAPIC_LVT_MASKED);
    /* LINT0/LINT1 left as firmware configured (ExtINT/NMI). */
}

void lapic_send_eoi(void) {
    if (g_lapic) {
        g_lapic[LAPIC_REG_EOI / 4] = 0;
    }
}
