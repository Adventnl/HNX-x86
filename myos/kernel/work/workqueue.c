/* Work queue implementation (see kernel/work/workqueue.h). */
#include "workqueue.h"

void work_init(struct work *w, void (*fn)(struct work *, void *), void *arg) {
    list_init(&w->link);
    w->fn = fn;
    w->arg = arg;
    w->pending = 0;
    w->run_count = 0;
}

void workqueue_init(struct workqueue *wq, const char *name) {
    list_init(&wq->items);
    spinlock_init(&wq->lock, name);
    wq->name = name;
    wq->queued = 0;
    wq->executed = 0;
}

int workqueue_queue(struct workqueue *wq, struct work *w) {
    uint64_t flags = spinlock_lock_irqsave(&wq->lock);
    int newly = 0;
    if (!w->pending) {
        w->pending = 1;
        list_add_tail(&w->link, &wq->items);
        wq->queued++;
        newly = 1;
    }
    spinlock_unlock_irqrestore(&wq->lock, flags);
    return newly;
}

int workqueue_cancel(struct workqueue *wq, struct work *w) {
    uint64_t flags = spinlock_lock_irqsave(&wq->lock);
    int removed = 0;
    if (w->pending) {
        list_del_init(&w->link);
        w->pending = 0;
        removed = 1;
    }
    spinlock_unlock_irqrestore(&wq->lock, flags);
    return removed;
}

int workqueue_run_pending(struct workqueue *wq) {
    int ran = 0;
    for (;;) {
        uint64_t flags = spinlock_lock_irqsave(&wq->lock);
        if (list_empty(&wq->items)) {
            spinlock_unlock_irqrestore(&wq->lock, flags);
            break;
        }
        struct list_node *n = wq->items.next;
        list_del_init(n);
        struct work *w = list_entry(n, struct work, link);
        w->pending = 0;
        spinlock_unlock_irqrestore(&wq->lock, flags);

        /* Run with the lock dropped so the callback may re-queue work. */
        if (w->fn) {
            w->fn(w, w->arg);
        }
        w->run_count++;
        wq->executed++;
        ran++;
    }
    return ran;
}

int workqueue_pending_count(struct workqueue *wq) {
    uint64_t flags = spinlock_lock_irqsave(&wq->lock);
    int n = (int)list_length(&wq->items);
    spinlock_unlock_irqrestore(&wq->lock, flags);
    return n;
}
