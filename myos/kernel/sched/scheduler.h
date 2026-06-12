/* Single-core round-robin scheduler. */
#ifndef MYOS_SCHED_SCHEDULER_H
#define MYOS_SCHED_SCHEDULER_H

#include "types.h"
#include "thread.h"

#define SCHEDULER_TIME_SLICE_TICKS 5    /* 5 ticks @ 100 Hz = 50 ms quantum */

void scheduler_init(void);
void scheduler_start(void) __attribute__((noreturn));
void scheduler_yield(void);
void scheduler_on_timer_tick(void);     /* timer IRQ handler context (IF=0)  */
struct thread *scheduler_current_thread(void);
uint64_t scheduler_switch_count(void);

/* Number of quantum-expiry preemptions actually taken. */
uint64_t scheduler_preempt_count(void);

/* Enqueue a thread on the ready queue (interrupt-safe). */
void scheduler_make_ready(struct thread *t);

/* Pick + switch to the next thread. Caller must hold IF=0. Used by sleep and
 * thread_exit; returns when this thread is scheduled again (never, if DEAD). */
void scheduler_reschedule(void);

/* Called by irq_dispatch after EOI: performs the deferred preemption switch. */
void scheduler_irq_exit(void);

#endif /* MYOS_SCHED_SCHEDULER_H */
