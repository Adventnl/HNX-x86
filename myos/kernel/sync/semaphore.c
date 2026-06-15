/* Counting semaphore implementation (see kernel/sync/semaphore.h). */
#include "semaphore.h"

void semaphore_init(struct semaphore *s, int initial, int max, const char *name) {
    s->count = initial;
    s->max = max;
    spinlock_init(&s->lock, name);
    waitqueue_init(&s->waiters, name);
    s->name = name;
}

int semaphore_trydown(struct semaphore *s) {
    uint64_t flags = spinlock_lock_irqsave(&s->lock);
    int ok = 0;
    if (s->count > 0) {
        s->count--;
        ok = 1;
    }
    spinlock_unlock_irqrestore(&s->lock, flags);
    return ok;
}

void semaphore_down(struct semaphore *s) {
    for (;;) {
        if (semaphore_trydown(s)) {
            return;
        }
        waitqueue_wait(&s->waiters);
    }
}

void semaphore_up(struct semaphore *s) {
    uint64_t flags = spinlock_lock_irqsave(&s->lock);
    s->count++;
    if (s->max > 0 && s->count > s->max) {
        s->count = s->max;
    }
    spinlock_unlock_irqrestore(&s->lock, flags);
    waitqueue_wake_one(&s->waiters);
}

int semaphore_count(const struct semaphore *s) {
    return s->count;
}
