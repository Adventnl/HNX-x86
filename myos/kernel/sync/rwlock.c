/* Reader-writer lock implementation (see kernel/sync/rwlock.h). */
#include "rwlock.h"

void rwlock_init(struct rwlock *rw, const char *name) {
    rw->state = 0;
    rw->waiting_writers = 0;
    spinlock_init(&rw->lock, name);
    waitqueue_init(&rw->readers, name);
    waitqueue_init(&rw->writers, name);
    rw->name = name;
}

int rwlock_read_trylock(struct rwlock *rw) {
    uint64_t flags = spinlock_lock_irqsave(&rw->lock);
    int ok = 0;
    /* Writer-preferring: defer readers while writers wait. */
    if (rw->state >= 0 && rw->waiting_writers == 0) {
        rw->state++;
        ok = 1;
    }
    spinlock_unlock_irqrestore(&rw->lock, flags);
    return ok;
}

void rwlock_read_lock(struct rwlock *rw) {
    while (!rwlock_read_trylock(rw)) {
        waitqueue_wait(&rw->readers);
    }
}

void rwlock_read_unlock(struct rwlock *rw) {
    uint64_t flags = spinlock_lock_irqsave(&rw->lock);
    if (rw->state > 0) {
        rw->state--;
    }
    int wake_writer = (rw->state == 0 && rw->waiting_writers > 0);
    spinlock_unlock_irqrestore(&rw->lock, flags);
    if (wake_writer) {
        waitqueue_wake_one(&rw->writers);
    }
}

int rwlock_write_trylock(struct rwlock *rw) {
    uint64_t flags = spinlock_lock_irqsave(&rw->lock);
    int ok = 0;
    if (rw->state == 0) {
        rw->state = -1;
        ok = 1;
    }
    spinlock_unlock_irqrestore(&rw->lock, flags);
    return ok;
}

void rwlock_write_lock(struct rwlock *rw) {
    uint64_t flags = spinlock_lock_irqsave(&rw->lock);
    rw->waiting_writers++;
    spinlock_unlock_irqrestore(&rw->lock, flags);

    while (!rwlock_write_trylock(rw)) {
        waitqueue_wait(&rw->writers);
    }

    flags = spinlock_lock_irqsave(&rw->lock);
    if (rw->waiting_writers > 0) {
        rw->waiting_writers--;
    }
    spinlock_unlock_irqrestore(&rw->lock, flags);
}

void rwlock_write_unlock(struct rwlock *rw) {
    uint64_t flags = spinlock_lock_irqsave(&rw->lock);
    rw->state = 0;
    int more_writers = rw->waiting_writers > 0;
    spinlock_unlock_irqrestore(&rw->lock, flags);
    /* Hand off to a waiting writer first (writer-preferring), else readers. */
    if (more_writers) {
        waitqueue_wake_one(&rw->writers);
    } else {
        waitqueue_wake_all(&rw->readers);
    }
}

int rwlock_reader_count(const struct rwlock *rw) {
    return rw->state > 0 ? rw->state : 0;
}

int rwlock_write_held(const struct rwlock *rw) {
    return rw->state < 0;
}
