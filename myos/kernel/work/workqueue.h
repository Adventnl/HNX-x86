/* Deferred work queue.
 *
 * A work item is a (function, arg) pair queued for later execution outside the
 * current context — the classic "do this, but not in the interrupt handler"
 * mechanism. Items are drained by workqueue_run_pending() (called from a worker
 * thread, the idle loop, or a test). Each item runs at most once per queue;
 * re-queueing an item that is still pending is a no-op.
 */
#ifndef MYOS_WORK_WORKQUEUE_H
#define MYOS_WORK_WORKQUEUE_H

#include "types.h"
#include "list.h"
#include "spinlock.h"

struct work {
    struct list_node link;
    void  (*fn)(struct work *self, void *arg);
    void   *arg;
    int     pending;
    uint64_t run_count;
};

struct workqueue {
    struct list_node items;
    struct spinlock  lock;
    const char      *name;
    uint64_t         queued;
    uint64_t         executed;
};

void work_init(struct work *w, void (*fn)(struct work *, void *), void *arg);

void workqueue_init(struct workqueue *wq, const char *name);
/* Queue an item; returns 1 if newly queued, 0 if already pending. */
int  workqueue_queue(struct workqueue *wq, struct work *w);
/* Cancel a pending item; returns 1 if it was removed before running. */
int  workqueue_cancel(struct workqueue *wq, struct work *w);
/* Run and dequeue every currently-pending item; returns the count run. */
int  workqueue_run_pending(struct workqueue *wq);
int  workqueue_pending_count(struct workqueue *wq);

#endif /* MYOS_WORK_WORKQUEUE_H */
