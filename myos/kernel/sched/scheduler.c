/* Single-core round-robin scheduler with a FIFO ready queue.
 *
 * Concurrency model: single CPU, no nesting (interrupt gates keep IF=0 inside
 * handlers). Every scheduler entry point runs with interrupts disabled —
 * either inherently (timer IRQ path) or via irq_save_flags_and_disable()
 * (yield/sleep paths).
 *
 * Preemption flow: the timer tick (IRQ context) only sets a need-resched
 * flag; irq_dispatch calls scheduler_irq_exit() after the LAPIC EOI, which
 * performs the actual context switch. The preempted thread's complete state
 * (iretq frame + GPRs from irq_stubs.S + callee-saved regs from
 * context_switch) stays on its own kernel stack until it is resumed. */
#include "scheduler.h"
#include "sleep.h"
#include "idle.h"
#include "irq.h"
#include "timer.h"
#include "cpu.h"
#include "log.h"
#include "panic.h"
#include "tss.h"
#include "paging.h"
#include "vmm.h"

static struct thread *g_ready_head;
static struct thread *g_ready_tail;

static struct thread *g_current;
static struct thread *g_idle;
static struct thread  g_boot_thread;    /* placeholder for the boot context */

static int g_started;
static volatile int g_need_resched;
static uint32_t g_slice;
static uint64_t g_switch_count;
static uint64_t g_preempt_count;
static uint64_t g_kernel_cr3;           /* kernel PML4; threads with cr3==0 use it */

/* Effective CR3 for a thread (0 means the kernel address space). */
static inline uint64_t thread_cr3(struct thread *t) {
    return t->cr3 ? t->cr3 : g_kernel_cr3;
}

static struct thread *ready_dequeue(void) {
    struct thread *t = g_ready_head;
    if (t) {
        g_ready_head = t->next;
        if (!g_ready_head) {
            g_ready_tail = NULL;
        }
        t->next = NULL;
    }
    return t;
}

void scheduler_make_ready(struct thread *t) {
    uint64_t f = irq_save_flags_and_disable();
    t->state = THREAD_READY;
    t->next = NULL;
    if (g_ready_tail) {
        g_ready_tail->next = t;
    } else {
        g_ready_head = t;
    }
    g_ready_tail = t;
    irq_restore_flags(f);
}

/* Core switch. IF must be 0. */
static void schedule(void) {
    struct thread *prev = g_current;
    struct thread *next = ready_dequeue();

    if (!next) {
        if (prev->state == THREAD_RUNNING) {
            g_slice = SCHEDULER_TIME_SLICE_TICKS;   /* only runnable thread */
            return;
        }
        next = g_idle;                              /* nothing runnable */
    }

    /* A still-running prev goes to the back of the queue (round robin).
     * The idle thread is never queued; it is the implicit fallback. */
    if (prev->state == THREAD_RUNNING && prev != g_idle) {
        prev->state = THREAD_READY;
        prev->next = NULL;
        if (g_ready_tail) {
            g_ready_tail->next = prev;
        } else {
            g_ready_head = prev;
        }
        g_ready_tail = prev;
    }

    if (next == prev) {
        prev->state = THREAD_RUNNING;
        g_slice = SCHEDULER_TIME_SLICE_TICKS;
        return;
    }

    g_current = next;
    next->state = THREAD_RUNNING;
    g_slice = SCHEDULER_TIME_SLICE_TICKS;
    g_switch_count++;

    /* Prompt 4: point the privilege-0 stack at the incoming thread's kernel
     * stack (so a ring 3 -> ring 0 trap lands there) and switch address spaces
     * only when it actually changes. Both kernel stacks stay mapped under both
     * CR3s (every user space mirrors the kernel footprint), so loading CR3
     * before the stack swap is safe. */
    tss_set_rsp0((uint64_t)(uintptr_t)next->kernel_stack_top);
    if (thread_cr3(next) != thread_cr3(prev)) {
        paging_load_cr3(thread_cr3(next));
    }

    context_switch(&prev->rsp, next->rsp);
    /* Execution resumes here when `prev` is scheduled again. */
}

void scheduler_init(void) {
    g_ready_head = g_ready_tail = NULL;
    g_switch_count = 0;
    g_preempt_count = 0;
    g_need_resched = 0;
    g_slice = SCHEDULER_TIME_SLICE_TICKS;
    g_kernel_cr3 = vmm_kernel_pml4();

    /* The current (boot) context becomes a throwaway placeholder thread so
     * the first context_switch has somewhere to save the old RSP. */
    g_boot_thread.id = 0;
    g_boot_thread.name = "boot";
    g_boot_thread.state = THREAD_RUNNING;
    g_current = &g_boot_thread;

    /* The idle thread always exists and is never placed in the ready queue. */
    g_idle = thread_create_raw("idle", idle_thread_entry, NULL);
    if (!g_idle) {
        kernel_panic("scheduler: cannot create idle thread");
    }
    g_idle->state = THREAD_READY;
}

void scheduler_start(void) {
    x86_cli();
    g_started = 1;
    /* The boot context is abandoned: never re-enqueued, never resumed. */
    g_boot_thread.state = THREAD_DEAD;
    schedule();
    kernel_panic("scheduler_start returned to boot context");
}

void scheduler_yield(void) {
    uint64_t f = irq_save_flags_and_disable();
    schedule();
    irq_restore_flags(f);
}

void scheduler_reschedule(void) {
    schedule();                  /* caller holds IF=0 */
}

void scheduler_on_timer_tick(void) {
    if (!g_started) {
        return;
    }
    sleep_wakeup_expired(kernel_ticks());
    if (g_slice > 0) {
        g_slice--;
    }
    if (g_slice == 0) {
        g_need_resched = 1;
    }
}

void scheduler_irq_exit(void) {
    if (!g_started || !g_need_resched) {
        return;
    }
    g_need_resched = 0;
    if (g_ready_head) {
        g_preempt_count++;       /* a real preemption: someone else can run */
    }
    schedule();
}

struct thread *scheduler_current_thread(void) {
    return g_current;
}

uint64_t scheduler_switch_count(void) {
    return g_switch_count;
}

uint64_t scheduler_preempt_count(void) {
    return g_preempt_count;
}
