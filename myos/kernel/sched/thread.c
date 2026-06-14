/* Kernel thread lifecycle. */
#include "thread.h"
#include "scheduler.h"
#include "heap.h"
#include "log.h"
#include "panic.h"
#include "cpu.h"
#include "kernel.h"

static uint64_t g_next_id = 1;

/* Kernel-stack recycling: the bump heap never frees, so reaped threads' 16 KiB
 * stacks are pooled here and reused. This bounds stack memory at the concurrent
 * thread count instead of the total threads created over the boot. */
#define MAX_FREE_STACKS 64
static uint64_t *g_free_stacks[MAX_FREE_STACKS];
static int g_free_stack_count;

static uint64_t *stack_alloc(void) {
    if (g_free_stack_count > 0) {
        return g_free_stacks[--g_free_stack_count];
    }
    return (uint64_t *)kmalloc(THREAD_KERNEL_STACK_SIZE);
}

static void stack_release(uint64_t *stack) {
    if (stack && g_free_stack_count < MAX_FREE_STACKS) {
        g_free_stacks[g_free_stack_count++] = stack;
    }
}

void thread_system_init(void) {
    g_next_id = 1;
    g_free_stack_count = 0;
}

void thread_trampoline(void) {
    /* First switch into this thread arrives with IF=0 (from a yield path or
     * the timer-IRQ path). Threads run with interrupts enabled. */
    x86_sti();
    struct thread *t = thread_current();
    t->entry(t->arg);
    thread_exit();
}

struct thread *thread_create_raw(const char *name, void (*entry)(void *), void *arg) {
    struct thread *t = (struct thread *)kcalloc(1, sizeof(struct thread));
    if (!t) {
        return NULL;
    }
    uint64_t *stack = stack_alloc();
    if (!stack) {
        return NULL;
    }

    t->id = g_next_id++;
    t->name = name;
    t->state = THREAD_NEW;
    t->entry = entry;
    t->arg = arg;
    t->kernel_stack_base = stack;

    /* 16-byte-aligned stack top. */
    uint64_t top = ((uint64_t)(uintptr_t)stack + THREAD_KERNEL_STACK_SIZE) & ~0xFULL;
    t->kernel_stack_top = (uint64_t *)(uintptr_t)top;

    /* Initial frame consumed by context_switch's restore path (see
     * context_switch.S). After the pops + ret, RSP = top-8, which satisfies
     * the SysV entry alignment (rsp % 16 == 8) for thread_trampoline. */
    uint64_t *sp = (uint64_t *)(uintptr_t)top;
    *--sp = 0;                                   /* alignment pad / stop slot */
    *--sp = (uint64_t)(uintptr_t)thread_trampoline;
    *--sp = 0;                                   /* rbp */
    *--sp = 0;                                   /* rbx */
    *--sp = 0;                                   /* r12 */
    *--sp = 0;                                   /* r13 */
    *--sp = 0;                                   /* r14 */
    *--sp = 0;                                   /* r15 */
    t->rsp = (uint64_t)(uintptr_t)sp;

    return t;
}

struct thread *thread_create(const char *name, void (*entry)(void *), void *arg) {
    struct thread *t = thread_create_raw(name, entry, arg);
    if (t) {
        scheduler_make_ready(t);
    }
    return t;
}

void thread_destroy(struct thread *thread) {
    if (!thread || thread->state != THREAD_DEAD) {
        return;                  /* only dead threads may be destroyed */
    }
    kfree(thread->kernel_stack_base);   /* no-op under the bump allocator */
    kfree(thread);
}

/* Recycle a DEAD thread's kernel stack for reuse (called by process reaping). */
void thread_reap(struct thread *thread) {
    if (!thread) {
        return;
    }
    stack_release(thread->kernel_stack_base);
    thread->kernel_stack_base = NULL;
    /* The small thread struct itself is left to the bump allocator. */
}

struct thread *thread_current(void) {
    return scheduler_current_thread();
}

uint64_t thread_current_id(void) {
    struct thread *t = thread_current();
    return t ? t->id : 0;
}

void thread_exit(void) {
    x86_cli();
    struct thread *t = thread_current();
    t->state = THREAD_DEAD;
    scheduler_reschedule();      /* switches away; never returns here */
    kernel_panic("thread_exit: dead thread resumed");
}
