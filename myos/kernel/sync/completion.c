/* Completion implementation (see kernel/sync/completion.h). */
#include "completion.h"

void completion_init(struct completion *c, const char *name) {
    c->done = 0;
    spinlock_init(&c->lock, name);
    waitqueue_init(&c->waiters, name);
    c->name = name;
}

void completion_reset(struct completion *c) {
    uint64_t flags = spinlock_lock_irqsave(&c->lock);
    c->done = 0;
    spinlock_unlock_irqrestore(&c->lock, flags);
}

int try_wait_for_completion(struct completion *c) {
    uint64_t flags = spinlock_lock_irqsave(&c->lock);
    int done = c->done;
    spinlock_unlock_irqrestore(&c->lock, flags);
    return done;
}

void wait_for_completion(struct completion *c) {
    while (!try_wait_for_completion(c)) {
        waitqueue_wait(&c->waiters);
    }
}

void complete(struct completion *c) {
    uint64_t flags = spinlock_lock_irqsave(&c->lock);
    c->done = 1;
    spinlock_unlock_irqrestore(&c->lock, flags);
    waitqueue_wake_one(&c->waiters);
}

void complete_all(struct completion *c) {
    uint64_t flags = spinlock_lock_irqsave(&c->lock);
    c->done = 1;
    spinlock_unlock_irqrestore(&c->lock, flags);
    waitqueue_wake_all(&c->waiters);
}

int completion_done(const struct completion *c) {
    return c->done;
}
