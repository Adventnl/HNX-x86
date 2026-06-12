/* Tick-based sleep / wakeup.
 *
 * Sleeping threads sit on a singly-linked list (reusing thread->next, which
 * is unused while not in the ready queue). The timer tick sweeps the list
 * and re-admits expired sleepers. */
#include "sleep.h"
#include "scheduler.h"
#include "thread.h"
#include "irq.h"
#include "timer.h"

static struct thread *g_sleep_head;
static uint64_t g_wakeup_count;

void thread_sleep_ticks(uint64_t ticks) {
    if (ticks == 0) {
        scheduler_yield();
        return;
    }
    uint64_t f = irq_save_flags_and_disable();
    struct thread *t = scheduler_current_thread();
    t->wake_tick = kernel_ticks() + ticks;
    t->state = THREAD_SLEEPING;
    t->next = g_sleep_head;
    g_sleep_head = t;
    scheduler_reschedule();      /* switches away; resumes here after wakeup */
    irq_restore_flags(f);
}

void thread_sleep_ms(uint64_t milliseconds) {
    uint64_t ticks = (milliseconds * KERNEL_TIMER_HZ + 999u) / 1000u;
    if (ticks == 0) {
        ticks = 1;
    }
    thread_sleep_ticks(ticks);
}

void sleep_wakeup_expired(uint64_t current_tick) {
    struct thread **pp = &g_sleep_head;
    while (*pp) {
        struct thread *t = *pp;
        if (t->wake_tick <= current_tick) {
            *pp = t->next;       /* unlink, then re-admit */
            t->next = NULL;
            scheduler_make_ready(t);
            g_wakeup_count++;
        } else {
            pp = &t->next;
        }
    }
}

uint64_t sleep_wakeup_count(void) {
    return g_wakeup_count;
}
