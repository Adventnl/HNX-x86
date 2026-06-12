/* Local APIC timer driver. */
#ifndef MYOS_X86_LAPIC_TIMER_H
#define MYOS_X86_LAPIC_TIMER_H

#include "types.h"

/* Initialize periodic LAPIC timer at `hz` using PIT-based calibration.
 * Returns 0 on success, -1 if calibration failed (caller falls back to PIT). */
int  lapic_timer_init(uint32_t hz);
void lapic_timer_stop(void);
uint64_t lapic_timer_ticks(void);

/* Calibrated LAPIC timer frequency (ticks/sec at the configured divider). */
uint64_t lapic_timer_calibrated_hz(void);

#endif /* MYOS_X86_LAPIC_TIMER_H */
