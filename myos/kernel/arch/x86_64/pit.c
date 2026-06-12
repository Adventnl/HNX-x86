/* PIT channel-0 driver.
 *
 * Two roles:
 *  1. Calibration reference for the LAPIC timer: pit_polled_delay() busy-waits
 *     a precise number of 1.193182 MHz input cycles by polling the down
 *     counter. No interrupt delivery needed.
 *  2. Fallback scheduler timer: if the LAPIC timer cannot be calibrated, the
 *     PIT runs periodic with the legacy PIC re-enabled for IRQ0 only
 *     (pit_irq_handler counts ticks and drives the kernel tick).
 *
 * Note: in the normal LAPIC-timer path the PIT IRQ line is never routed (the
 * PIC is disabled and no I/O APIC is programmed in Prompt 3), so pit_ticks()
 * only advances in the fallback path. */
#include "pit.h"
#include "cpu.h"
#include "pic.h"
#include "timer.h"

#define PIT_CH0_DATA 0x40
#define PIT_CMD      0x43

/* Command bits: channel 0, access lobyte/hibyte. */
#define PIT_CMD_CH0_LOHI_MODE2 0x34   /* mode 2: rate generator      */
#define PIT_CMD_CH0_LOHI_MODE0 0x30   /* mode 0: one-shot (stop-ish) */
#define PIT_CMD_LATCH_CH0      0x00

static volatile uint64_t g_pit_ticks;

static uint16_t pit_read_counter(void) {
    x86_outb(PIT_CMD, PIT_CMD_LATCH_CH0);
    uint8_t lo = x86_inb(PIT_CH0_DATA);
    uint8_t hi = x86_inb(PIT_CH0_DATA);
    return (uint16_t)((hi << 8) | lo);
}

void pit_init_periodic(uint32_t hz) {
    uint32_t divisor;
    if (hz == 0) {
        divisor = 0;                       /* 0 == 65536: slowest (18.2 Hz) */
    } else {
        divisor = PIT_BASE_FREQUENCY / hz;
        if (divisor < 1) {
            divisor = 1;
        }
        if (divisor > 65535) {
            divisor = 0;                   /* clamp to slowest */
        }
    }
    x86_outb(PIT_CMD, PIT_CMD_CH0_LOHI_MODE2);
    x86_outb(PIT_CH0_DATA, (uint8_t)(divisor & 0xFF));
    x86_outb(PIT_CH0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
}

void pit_stop(void) {
    /* The 8254 has no true stop; mode 0 with a max count effectively idles
     * the channel (one distant terminal count, no periodic output). */
    x86_outb(PIT_CMD, PIT_CMD_CH0_LOHI_MODE0);
    x86_outb(PIT_CH0_DATA, 0xFF);
    x86_outb(PIT_CH0_DATA, 0xFF);
}

uint64_t pit_ticks(void) {
    return g_pit_ticks;
}

void pit_polled_delay(uint32_t pit_cycles) {
    /* Run the channel as a free-running rate generator over the full 16-bit
     * range and accumulate down-counter deltas (wrap-safe as long as each
     * poll gap is < 54.9 ms, which a busy poll loop trivially satisfies). */
    pit_init_periodic(0);                  /* divisor 65536 */
    uint16_t last = pit_read_counter();
    uint64_t elapsed = 0;
    while (elapsed < pit_cycles) {
        uint16_t now = pit_read_counter();
        elapsed += (uint16_t)(last - now); /* down counter: delta mod 2^16 */
        last = now;
    }
}

void pit_irq_handler(uint8_t vector, void *context) {
    (void)vector;
    (void)context;
    g_pit_ticks++;
    pic_send_eoi(0);                       /* fallback path uses the 8259 */
    kernel_timer_on_tick();
}
