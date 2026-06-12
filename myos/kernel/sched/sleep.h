/* Tick-based sleep / wakeup. */
#ifndef MYOS_SCHED_SLEEP_H
#define MYOS_SCHED_SLEEP_H

#include "types.h"

void thread_sleep_ticks(uint64_t ticks);
void thread_sleep_ms(uint64_t milliseconds);

/* Move every sleeper with wake_tick <= current_tick to the ready queue.
 * Called from the timer tick (IF=0). */
void sleep_wakeup_expired(uint64_t current_tick);

/* Number of sleep->ready wakeups performed (test instrumentation). */
uint64_t sleep_wakeup_count(void);

#endif /* MYOS_SCHED_SLEEP_H */
