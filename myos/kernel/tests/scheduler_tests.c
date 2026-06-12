/* Scheduler self-tests.
 *
 * Threads:
 *   A       — increments its counter and yields (cooperative churn).
 *   B       — increments its counter and sleeps 20 ms (sleep/wakeup traffic).
 *   C       — busy-spins for ~8 ticks per round before yielding, guaranteeing
 *             at least one quantum-expiry preemption per round while A stays
 *             runnable.
 *   checker — sleeps, then validates counters/instrumentation and prints the
 *             [TEST]/[PASS] protocol. Any failure panics. */
#include "scheduler_tests.h"
#include "thread.h"
#include "scheduler.h"
#include "sleep.h"
#include "timer.h"
#include "irq.h"
#include "log.h"
#include "panic.h"

static volatile uint64_t g_count_a;
static volatile uint64_t g_count_b;
static volatile uint64_t g_count_c;
static volatile int g_stop_workers;

static void fail(const char *name) {
    kernel_log("[PANIC] early test failed: ");
    kernel_log_line(name);
    kernel_panic("scheduler test failed");
}

static void thread_a_entry(void *arg) {
    (void)arg;
    while (!g_stop_workers) {
        g_count_a++;
        scheduler_yield();
    }
    thread_exit();
}

static void thread_b_entry(void *arg) {
    (void)arg;
    while (!g_stop_workers) {
        g_count_b++;
        thread_sleep_ms(20);
    }
    thread_exit();
}

static void thread_c_entry(void *arg) {
    (void)arg;
    while (!g_stop_workers) {
        /* Busy phase: hold the CPU well past one 5-tick quantum so the timer
         * must preempt us (A is runnable the whole time). */
        uint64_t start = kernel_ticks();
        while (kernel_ticks() < start + 8 && !g_stop_workers) {
            g_count_c++;
        }
        scheduler_yield();
    }
    thread_exit();
}

static void checker_entry(void *arg) {
    (void)arg;
    uint64_t ticks_at_start = kernel_ticks();

    kernel_log_line("[TEST] scheduler round-robin");
    thread_sleep_ms(300);

    if (g_count_a == 0) fail("scheduler round-robin (thread A never ran)");
    if (g_count_b == 0) fail("scheduler round-robin (thread B never ran)");
    if (g_count_c == 0) fail("scheduler round-robin (thread C never ran)");
    if (scheduler_switch_count() == 0) fail("scheduler round-robin (no switches)");
    if (kernel_ticks() <= ticks_at_start) fail("scheduler round-robin (ticks frozen)");
    kernel_log_line("[PASS] scheduler round-robin");
    kernel_log_ok("Kernel ticks increasing");

    kernel_log_line("[TEST] sleep/wakeup");
    /* B sleeps every 20 ms and this thread slept 300 ms — wakeups must have
     * happened. */
    if (sleep_wakeup_count() == 0) fail("sleep/wakeup (no wakeups recorded)");
    kernel_log_line("[PASS] sleep/wakeup");

    kernel_log_line("[TEST] timer preemption");
    /* Give C another busy window, then check the preemption counter. */
    thread_sleep_ms(200);
    if (scheduler_preempt_count() == 0) fail("timer preemption (never preempted)");
    kernel_log_line("[PASS] timer preemption");

    g_stop_workers = 1;
    kernel_log_ok("Scheduler tests passed");

    kernel_log_line("");
    kernel_log_hex64("    kernel ticks   : ", kernel_ticks());
    kernel_log_hex64("    uptime (ms)    : ", kernel_uptime_ms());
    kernel_log_hex64("    switches       : ", scheduler_switch_count());
    kernel_log_hex64("    preemptions    : ", scheduler_preempt_count());
    kernel_log_hex64("    wakeups        : ", sleep_wakeup_count());
    kernel_log_hex64("    timer irqs     : ", irq_count_for_vector(LAPIC_TIMER_VECTOR));
    kernel_log_line("");
    kernel_log_line("MyOS scheduler running. System idle.");

    for (;;) {
        thread_sleep_ms(1000);
    }
}

void scheduler_tests_start(void) {
    if (!thread_create("test-A", thread_a_entry, NULL) ||
        !thread_create("test-B", thread_b_entry, NULL) ||
        !thread_create("test-C", thread_c_entry, NULL) ||
        !thread_create("sched-checker", checker_entry, NULL)) {
        kernel_panic("scheduler tests: thread creation failed");
    }
}
