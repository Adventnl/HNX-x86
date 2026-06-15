/* Sleeping mutex implementation (see kernel/sync/mutex.h). */
#include "mutex.h"
#include "thread.h"

void mutex_init(struct mutex *m, const char *name) {
    atomic_set(&m->locked, 0);
    m->owner = NULL;
    waitqueue_init(&m->waiters, name);
    m->name = name;
    m->acquisitions = 0;
    m->contended = 0;
}

int mutex_trylock(struct mutex *m) {
    if (atomic_cas(&m->locked, 0, 1)) {
        m->owner = thread_current();
        m->acquisitions++;
        return 1;
    }
    return 0;
}

void mutex_lock(struct mutex *m) {
    if (mutex_trylock(m)) {
        return;
    }
    /* Contended: park until the holder releases, then retry. */
    m->contended++;
    while (!mutex_trylock(m)) {
        waitqueue_wait(&m->waiters);
    }
}

void mutex_unlock(struct mutex *m) {
    m->owner = NULL;
    atomic_set(&m->locked, 0);
    waitqueue_wake_one(&m->waiters);
}

int mutex_is_locked(const struct mutex *m) {
    return atomic_read((atomic_t *)&m->locked) != 0;
}

struct thread *mutex_owner(const struct mutex *m) {
    return m->owner;
}
