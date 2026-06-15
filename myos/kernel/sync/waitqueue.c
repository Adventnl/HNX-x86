/* Wait queue implementation (see kernel/sync/waitqueue.h). */
#include "waitqueue.h"
#include "thread.h"
#include "scheduler.h"
#include "irq.h"

void waitqueue_init(struct waitqueue *wq, const char *name) {
    list_init(&wq->waiters);
    spinlock_init(&wq->lock, name);
    wq->wakeups = 0;
}

int waitqueue_empty(struct waitqueue *wq) {
    return list_empty(&wq->waiters);
}

size_t waitqueue_len(struct waitqueue *wq) {
    return list_length(&wq->waiters);
}

void waitqueue_enqueue(struct waitqueue *wq, struct wait_entry *e) {
    uint64_t flags = spinlock_lock_irqsave(&wq->lock);
    e->woken = 0;
    list_add_tail(&e->link, &wq->waiters);
    spinlock_unlock_irqrestore(&wq->lock, flags);
}

struct wait_entry *waitqueue_dequeue(struct waitqueue *wq) {
    uint64_t flags = spinlock_lock_irqsave(&wq->lock);
    struct wait_entry *e = NULL;
    if (!list_empty(&wq->waiters)) {
        struct list_node *n = wq->waiters.next;
        list_del_init(n);
        e = list_entry(n, struct wait_entry, link);
    }
    spinlock_unlock_irqrestore(&wq->lock, flags);
    return e;
}

void waitqueue_remove(struct waitqueue *wq, struct wait_entry *e) {
    uint64_t flags = spinlock_lock_irqsave(&wq->lock);
    if (list_linked(&e->link)) {
        list_del_init(&e->link);
    }
    spinlock_unlock_irqrestore(&wq->lock, flags);
}

void waitqueue_wait(struct waitqueue *wq) {
    struct wait_entry e;
    list_init(&e.link);
    e.thread = thread_current();
    e.woken = 0;

    uint64_t flags = irq_save_flags_and_disable();
    list_add_tail(&e.link, &wq->waiters);
    if (e.thread) {
        e.thread->state = THREAD_BLOCKED;
    }
    /* Reschedule with interrupts off; we return here once made ready again. */
    scheduler_reschedule();

    /* Woken: ensure we are no longer linked. */
    if (list_linked(&e.link)) {
        list_del_init(&e.link);
    }
    irq_restore_flags(flags);
}

static int wake_entry(struct waitqueue *wq, struct wait_entry *e) {
    e->woken = 1;
    wq->wakeups++;
    if (e->thread) {
        e->thread->state = THREAD_READY;
        scheduler_make_ready(e->thread);
    }
    return 1;
}

int waitqueue_wake_one(struct waitqueue *wq) {
    struct wait_entry *e = waitqueue_dequeue(wq);
    if (!e) {
        return 0;
    }
    return wake_entry(wq, e);
}

int waitqueue_wake_all(struct waitqueue *wq) {
    int n = 0;
    struct wait_entry *e;
    while ((e = waitqueue_dequeue(wq)) != NULL) {
        n += wake_entry(wq, e);
    }
    return n;
}
