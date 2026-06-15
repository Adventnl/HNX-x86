/* Sleeping mutex: a binary lock whose contended path blocks on a wait queue
 * instead of spinning. Tracks the owning thread so re-entrant misuse and
 * "unlock by non-owner" can be detected.
 */
#ifndef MYOS_SYNC_MUTEX_H
#define MYOS_SYNC_MUTEX_H

#include "types.h"
#include "atomic.h"
#include "waitqueue.h"

struct thread;

struct mutex {
    atomic_t          locked;     /* 0 = free, 1 = held */
    struct thread    *owner;
    struct waitqueue  waiters;
    const char       *name;
    uint64_t          acquisitions;
    uint64_t          contended;
};

void mutex_init(struct mutex *m, const char *name);

void mutex_lock(struct mutex *m);
int  mutex_trylock(struct mutex *m);     /* 1 on success, 0 if held */
void mutex_unlock(struct mutex *m);

int  mutex_is_locked(const struct mutex *m);
struct thread *mutex_owner(const struct mutex *m);

#endif /* MYOS_SYNC_MUTEX_H */
