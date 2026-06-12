/* Kernel timer abstraction + tick counter. */
#ifndef MYOS_X86_TIMER_H
#define MYOS_X86_TIMER_H

#include "types.h"

#define KERNEL_TIMER_HZ 100u           /* 1 tick = 10 ms */

/* Pick a tick source (LAPIC timer preferred, PIT+PIC fallback) and start it. */
void kernel_timer_init(void);

uint64_t kernel_ticks(void);
uint64_t kernel_uptime_ms(void);

/* Called from the active timer IRQ handler once per tick. */
void kernel_timer_on_tick(void);

/* 1 if the LAPIC timer drives the tick, 0 if the PIT fallback does. */
int kernel_timer_uses_lapic(void);

#endif /* MYOS_X86_TIMER_H */
