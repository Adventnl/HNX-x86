/* Kernel timer abstraction.
 *
 * Preferred source: LAPIC timer (periodic, PIT-calibrated, vector 0x30).
 * Fallback: if LAPIC calibration fails, re-enable the legacy PIC for IRQ0
 * only and run the PIT periodic at KERNEL_TIMER_HZ. The fallback is logged
 * loudly; in QEMU the LAPIC path is the one exercised. */
#include "timer.h"
#include "lapic_timer.h"
#include "pit.h"
#include "pic.h"
#include "irq.h"
#include "log.h"
#include "scheduler.h"

static volatile uint64_t g_ticks;
static int g_uses_lapic;

void kernel_timer_on_tick(void) {
    g_ticks++;
    scheduler_on_timer_tick();
}

void kernel_timer_init(void) {
    if (lapic_timer_init(KERNEL_TIMER_HZ) == 0) {
        g_uses_lapic = 1;
        kernel_log_ok("Local APIC timer online");
    } else {
        /* PIT fallback: route legacy IRQ0 through the (re-armed) 8259. */
        g_uses_lapic = 0;
        kernel_log_warn("Local APIC timer unavailable, using PIT scheduler timer");
        irq_register_handler(IRQ_BASE_VECTOR + 0, pit_irq_handler, NULL);
        pic_remap(0x20, 0x28);
        pic_unmask_irq(0);
        pit_init_periodic(KERNEL_TIMER_HZ);
    }
}

uint64_t kernel_ticks(void) {
    return g_ticks;
}

uint64_t kernel_uptime_ms(void) {
    return g_ticks * (1000u / KERNEL_TIMER_HZ);
}

int kernel_timer_uses_lapic(void) {
    return g_uses_lapic;
}
