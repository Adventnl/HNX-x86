/* Spinlock implementation (see kernel/sync/spinlock.h). */
#include "spinlock.h"
#include "irq.h"

void spinlock_init(struct spinlock *l, const char *name) {
    atomic_set(&l->locked, 0);
    l->name = name;
    l->owner_cpu = 0;
    l->acquisitions = 0;
    l->contended = 0;
}

void spinlock_lock(struct spinlock *l) {
    /* Fast path: atomically claim a free lock. */
    if (atomic_xchg(&l->locked, 1) != 0) {
        l->contended++;
        /* Slow path: spin until released. On UP this only happens via an
         * interrupt handler that must be using the irqsave variant, so this
         * path is effectively unreachable, but it keeps the lock SMP-correct. */
        while (atomic_xchg(&l->locked, 1) != 0) {
            cpu_relax();
        }
    }
    l->acquisitions++;
}

int spinlock_trylock(struct spinlock *l) {
    if (atomic_xchg(&l->locked, 1) == 0) {
        l->acquisitions++;
        return 1;
    }
    return 0;
}

void spinlock_unlock(struct spinlock *l) {
    smp_wmb();
    atomic_set(&l->locked, 0);
}

int spinlock_held(const struct spinlock *l) {
    return atomic_read((atomic_t *)&l->locked) != 0;
}

uint64_t spinlock_lock_irqsave(struct spinlock *l) {
    uint64_t flags = irq_save_flags_and_disable();
    spinlock_lock(l);
    return flags;
}

void spinlock_unlock_irqrestore(struct spinlock *l, uint64_t flags) {
    spinlock_unlock(l);
    irq_restore_flags(flags);
}
