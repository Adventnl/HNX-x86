/* Ticket-free test-and-set spinlocks with IRQ-save variants.
 *
 * On the current single-CPU kernel a spinlock is really a guard against
 * re-entrancy from interrupt handlers, so the IRQ-save variant (disable
 * interrupts, take the lock) is the common case. The plain lock body still
 * uses an atomic exchange so the code is SMP-ready.
 *
 * Lock-order diagnostics: each lock carries a name and (in debug builds) the
 * acquisition is recorded so a simple deadlock/ordering check can run.
 */
#ifndef MYOS_SYNC_SPINLOCK_H
#define MYOS_SYNC_SPINLOCK_H

#include "types.h"
#include "atomic.h"

struct spinlock {
    atomic_t      locked;       /* 0 = free, 1 = held */
    const char   *name;
    uint64_t      owner_cpu;    /* reserved for SMP */
    uint64_t      acquisitions; /* statistics */
    uint64_t      contended;    /* times the fast path missed */
};

#define SPINLOCK_INIT(nm) { ATOMIC_INIT(0), (nm), 0, 0, 0 }

void spinlock_init(struct spinlock *l, const char *name);

void spinlock_lock(struct spinlock *l);
int  spinlock_trylock(struct spinlock *l);    /* 1 on success, 0 if held */
void spinlock_unlock(struct spinlock *l);
int  spinlock_held(const struct spinlock *l);

/* IRQ-safe: disable interrupts, take the lock, return the saved flags. */
uint64_t spinlock_lock_irqsave(struct spinlock *l);
void     spinlock_unlock_irqrestore(struct spinlock *l, uint64_t flags);

#endif /* MYOS_SYNC_SPINLOCK_H */
