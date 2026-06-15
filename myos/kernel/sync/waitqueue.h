/* Wait queues: the blocking primitive every higher-level sleep object is built
 * on (mutex, semaphore, completion, condition variables, blocking I/O).
 *
 * A waiter is an on-stack wait_entry that the blocking thread links into the
 * queue before descheduling. A waker dequeues entries and hands the parked
 * threads back to the scheduler's ready queue. The low-level enqueue/dequeue
 * ops are independent of the scheduler so they can be unit-tested directly.
 */
#ifndef MYOS_SYNC_WAITQUEUE_H
#define MYOS_SYNC_WAITQUEUE_H

#include "types.h"
#include "list.h"
#include "spinlock.h"

struct thread;

struct wait_entry {
    struct list_node link;
    struct thread   *thread;   /* parked thread (NULL in pure-list tests) */
    int              woken;    /* set by the waker */
};

struct waitqueue {
    struct list_node waiters;
    struct spinlock  lock;
    uint64_t         wakeups;  /* statistics */
};

void   waitqueue_init(struct waitqueue *wq, const char *name);
int    waitqueue_empty(struct waitqueue *wq);
size_t waitqueue_len(struct waitqueue *wq);

/* Low-level list ops (no scheduler interaction). */
void               waitqueue_enqueue(struct waitqueue *wq, struct wait_entry *e);
struct wait_entry *waitqueue_dequeue(struct waitqueue *wq);
void               waitqueue_remove(struct waitqueue *wq, struct wait_entry *e);

/* High-level: park the current thread until woken (used by real threads only;
 * must not be called from the boot context before the scheduler is running). */
void waitqueue_wait(struct waitqueue *wq);

/* Wake the oldest waiter / all waiters; returns the number woken. */
int  waitqueue_wake_one(struct waitqueue *wq);
int  waitqueue_wake_all(struct waitqueue *wq);

#endif /* MYOS_SYNC_WAITQUEUE_H */
