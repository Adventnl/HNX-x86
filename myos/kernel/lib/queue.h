/* Generic intrusive FIFO queue built on the doubly-linked list.
 *
 * A thin, self-documenting wrapper over list.h that names the operations in
 * queue terms (enqueue/dequeue/peek) for code that wants a work/run queue
 * without re-deriving the list idioms each time.
 */
#ifndef MYOS_LIB_QUEUE_H
#define MYOS_LIB_QUEUE_H

#include "list.h"

struct queue {
    struct list_node head;
    size_t           length;
};

static inline void queue_init(struct queue *q) {
    list_init(&q->head);
    q->length = 0;
}

static inline int queue_empty(const struct queue *q) {
    return q->length == 0;
}

static inline size_t queue_length(const struct queue *q) {
    return q->length;
}

/* Append to the tail. */
static inline void queue_enqueue(struct queue *q, struct list_node *n) {
    list_add_tail(n, &q->head);
    q->length++;
}

/* Push to the head (priority / urgent). */
static inline void queue_push_front(struct queue *q, struct list_node *n) {
    list_add(n, &q->head);
    q->length++;
}

/* Remove and return the head node, or NULL if empty. */
static inline struct list_node *queue_dequeue(struct queue *q) {
    if (queue_empty(q)) {
        return NULL;
    }
    struct list_node *n = q->head.next;
    list_del_init(n);
    q->length--;
    return n;
}

static inline struct list_node *queue_peek(const struct queue *q) {
    return queue_empty(q) ? NULL : q->head.next;
}

/* Remove an arbitrary node known to be on this queue. */
static inline void queue_remove(struct queue *q, struct list_node *n) {
    list_del_init(n);
    if (q->length) {
        q->length--;
    }
}

#define queue_entry(node, type, member) list_entry(node, type, member)

#endif /* MYOS_LIB_QUEUE_H */
