/* 8253/8254 PIT driver (channel 0). */
#ifndef MYOS_X86_PIT_H
#define MYOS_X86_PIT_H

#include "types.h"

#define PIT_BASE_FREQUENCY 1193182u

void pit_init_periodic(uint32_t hz);
void pit_stop(void);
uint64_t pit_ticks(void);

/* Busy-wait for `pit_cycles` input-clock cycles by polling the channel-0
 * counter (no interrupts required). Used to calibrate the LAPIC timer.
 * 1 ms ~= 1193 cycles. */
void pit_polled_delay(uint32_t pit_cycles);

/* IRQ handler for the PIT-fallback timer path (vector IRQ_BASE_VECTOR + 0). */
void pit_irq_handler(uint8_t vector, void *context);

#endif /* MYOS_X86_PIT_H */
