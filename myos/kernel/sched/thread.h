/* Kernel threads (no user mode, no processes). */
#ifndef MYOS_SCHED_THREAD_H
#define MYOS_SCHED_THREAD_H

#include "types.h"

#define THREAD_KERNEL_STACK_SIZE 16384

enum thread_state {
    THREAD_NEW,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_SLEEPING,
    THREAD_BLOCKED,
    THREAD_DEAD
};

struct thread {
    uint64_t id;
    const char *name;
    enum thread_state state;

    uint64_t *kernel_stack_base;
    uint64_t *kernel_stack_top;
    uint64_t rsp;                  /* saved stack pointer when not running */

    uint64_t wake_tick;            /* absolute tick to wake at (SLEEPING)  */

    /* Prompt 4: user-mode integration. cr3 == 0 means "kernel address space";
     * a non-zero value is the user PML4 loaded when this thread is scheduled.
     * proc back-points at the owning struct process (NULL for kernel threads),
     * so syscalls can find the current process. */
    uint64_t cr3;
    void *proc;

    void (*entry)(void *);
    void *arg;

    struct thread *next;           /* ready-queue / sleep-list link        */
};

void thread_system_init(void);

/* Create a thread and admit it to the scheduler's ready queue. */
struct thread *thread_create(const char *name, void (*entry)(void *), void *arg);

/* Create without admitting (used by the scheduler for the idle thread). */
struct thread *thread_create_raw(const char *name, void (*entry)(void *), void *arg);

void thread_destroy(struct thread *thread);
/* Recycle a DEAD thread's kernel stack into the reuse pool. */
void thread_reap(struct thread *thread);
struct thread *thread_current(void);
uint64_t thread_current_id(void);
void thread_exit(void) __attribute__((noreturn));

/* First code every new thread runs (set up by thread_create_raw). */
void thread_trampoline(void);

/* context_switch.S: save callee-saved regs + rsp into *old_rsp, switch to
 * new_rsp, restore, return into the new thread's saved context. */
void context_switch(uint64_t *old_rsp, uint64_t new_rsp);

#endif /* MYOS_SCHED_THREAD_H */
