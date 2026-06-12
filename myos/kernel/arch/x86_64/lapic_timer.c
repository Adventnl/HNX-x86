/* Local APIC timer: PIT-calibrated periodic scheduler tick source.
 *
 * Calibration: run the LAPIC timer one-shot (masked, divider 16) from
 * 0xFFFFFFFF while busy-waiting 50 ms on the PIT, then scale the observed
 * countdown to ticks/second. The periodic initial count for the requested
 * rate follows directly. */
#include "lapic_timer.h"
#include "apic.h"
#include "irq.h"
#include "pit.h"
#include "timer.h"

#define CALIBRATE_MS 50

static volatile uint64_t g_ticks;
static uint64_t g_calibrated_hz;     /* LAPIC timer ticks/sec at divider 16 */

static void lapic_timer_irq(uint8_t vector, void *context) {
    (void)vector;
    (void)context;
    g_ticks++;
    kernel_timer_on_tick();
}

int lapic_timer_init(uint32_t hz) {
    if (hz == 0) {
        return -1;
    }

    /* --- Calibrate against the PIT --- */
    lapic_write(LAPIC_REG_TIMER_DIVIDE, LAPIC_TIMER_DIVIDE_16);
    lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_LVT_MASKED);   /* one-shot, masked */
    lapic_write(LAPIC_REG_TIMER_INIT, 0xFFFFFFFFu);

    pit_polled_delay((PIT_BASE_FREQUENCY / 1000u) * CALIBRATE_MS);

    uint32_t remaining = lapic_read(LAPIC_REG_TIMER_CURRENT);
    lapic_write(LAPIC_REG_TIMER_INIT, 0);                 /* stop one-shot */

    uint64_t elapsed = 0xFFFFFFFFu - remaining;
    if (elapsed == 0 || elapsed == 0xFFFFFFFFu) {
        return -1;                       /* timer not counting: unusable */
    }
    g_calibrated_hz = elapsed * (1000u / CALIBRATE_MS);

    uint64_t initial = g_calibrated_hz / hz;
    if (initial == 0) {
        return -1;
    }

    /* --- Program periodic mode on our vector --- */
    irq_register_handler(LAPIC_TIMER_VECTOR, lapic_timer_irq, NULL);
    lapic_write(LAPIC_REG_TIMER_DIVIDE, LAPIC_TIMER_DIVIDE_16);
    lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_TIMER_VECTOR | LAPIC_LVT_TIMER_PERIODIC);
    lapic_write(LAPIC_REG_TIMER_INIT, (uint32_t)initial);
    return 0;
}

void lapic_timer_stop(void) {
    lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_REG_TIMER_INIT, 0);
}

uint64_t lapic_timer_ticks(void) {
    return g_ticks;
}

uint64_t lapic_timer_calibrated_hz(void) {
    return g_calibrated_hz;
}
