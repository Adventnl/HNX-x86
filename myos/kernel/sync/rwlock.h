/* Reader-writer lock (writer-preferring on the contended path).
 *
 * Multiple readers may hold the lock concurrently; a writer needs exclusive
 * access. Implemented with a count: >0 means N readers, -1 means one writer,
 * 0 means free. Contended acquirers park on a wait queue.
 */
#ifndef MYOS_SYNC_RWLOCK_H
#define MYOS_SYNC_RWLOCK_H

#include "types.h"
#include "spinlock.h"
#include "waitqueue.h"

struct rwlock {
    int               state;        /* -1 writer, 0 free, >0 reader count */
    int               waiting_writers;
    struct spinlock   lock;
    struct waitqueue  readers;
    struct waitqueue  writers;
    const char       *name;
};

void rwlock_init(struct rwlock *rw, const char *name);

void rwlock_read_lock(struct rwlock *rw);
int  rwlock_read_trylock(struct rwlock *rw);
void rwlock_read_unlock(struct rwlock *rw);

void rwlock_write_lock(struct rwlock *rw);
int  rwlock_write_trylock(struct rwlock *rw);
void rwlock_write_unlock(struct rwlock *rw);

int  rwlock_reader_count(const struct rwlock *rw);
int  rwlock_write_held(const struct rwlock *rw);

#endif /* MYOS_SYNC_RWLOCK_H */
