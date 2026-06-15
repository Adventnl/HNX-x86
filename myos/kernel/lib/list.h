/* Intrusive doubly-linked circular list.
 *
 * Modeled on the classic kernel list_head: the link node is embedded inside the
 * containing structure and `list_entry` recovers the owner via offsetof. A list
 * is represented by a sentinel head node whose next/prev point back to itself
 * when empty, which removes all the NULL-checking from insert/remove.
 */
#ifndef MYOS_LIB_LIST_H
#define MYOS_LIB_LIST_H

#include "types.h"

struct list_node {
    struct list_node *next;
    struct list_node *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_node name = LIST_HEAD_INIT(name)

static inline void list_init(struct list_node *n) {
    n->next = n;
    n->prev = n;
}

static inline int list_empty(const struct list_node *head) {
    return head->next == head;
}

static inline void __list_add(struct list_node *n,
                              struct list_node *prev,
                              struct list_node *next) {
    next->prev = n;
    n->next = next;
    n->prev = prev;
    prev->next = n;
}

/* Insert just after head (stack/LIFO push, or front of a queue). */
static inline void list_add(struct list_node *n, struct list_node *head) {
    __list_add(n, head, head->next);
}

/* Insert just before head (FIFO tail append). */
static inline void list_add_tail(struct list_node *n, struct list_node *head) {
    __list_add(n, head->prev, head);
}

static inline void __list_del(struct list_node *prev, struct list_node *next) {
    next->prev = prev;
    prev->next = next;
}

static inline void list_del(struct list_node *n) {
    __list_del(n->prev, n->next);
    n->next = NULL;
    n->prev = NULL;
}

/* Remove and re-init so the node can be safely re-added/tested. */
static inline void list_del_init(struct list_node *n) {
    __list_del(n->prev, n->next);
    list_init(n);
}

/* Is this node currently linked into some list? (requires del_init/init). */
static inline int list_linked(const struct list_node *n) {
    return n->next != n && n->next != NULL;
}

static inline void list_move(struct list_node *n, struct list_node *head) {
    __list_del(n->prev, n->next);
    list_add(n, head);
}

static inline void list_move_tail(struct list_node *n, struct list_node *head) {
    __list_del(n->prev, n->next);
    list_add_tail(n, head);
}

static inline int list_is_first(const struct list_node *n,
                                const struct list_node *head) {
    return n->prev == head;
}

static inline int list_is_last(const struct list_node *n,
                               const struct list_node *head) {
    return n->next == head;
}

/* Splice src (non-empty) into dst right after the head, then re-init src. */
static inline void list_splice_init(struct list_node *src,
                                    struct list_node *dst) {
    if (list_empty(src)) {
        return;
    }
    struct list_node *first = src->next;
    struct list_node *last = src->prev;
    first->prev = dst;
    last->next = dst->next;
    dst->next->prev = last;
    dst->next = first;
    list_init(src);
}

#ifndef offsetof
#define offsetof(type, member) ((size_t)&((type *)0)->member)
#endif

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#define list_entry(ptr, type, member) container_of(ptr, type, member)

#define list_first_entry(head, type, member) \
    list_entry((head)->next, type, member)
#define list_last_entry(head, type, member) \
    list_entry((head)->prev, type, member)

#define list_for_each(pos, head) \
    for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)

#define list_for_each_safe(pos, tmp, head)                 \
    for ((pos) = (head)->next, (tmp) = (pos)->next;        \
         (pos) != (head);                                  \
         (pos) = (tmp), (tmp) = (pos)->next)

#define list_for_each_entry(pos, head, member)                              \
    for ((pos) = list_entry((head)->next, typeof(*(pos)), member);          \
         &(pos)->member != (head);                                          \
         (pos) = list_entry((pos)->member.next, typeof(*(pos)), member))

#define list_for_each_entry_safe(pos, tmp, head, member)                    \
    for ((pos) = list_entry((head)->next, typeof(*(pos)), member),          \
         (tmp) = list_entry((pos)->member.next, typeof(*(pos)), member);    \
         &(pos)->member != (head);                                          \
         (pos) = (tmp),                                                     \
         (tmp) = list_entry((tmp)->member.next, typeof(*(tmp)), member))

/* Out-of-line helpers (kernel/lib/list.c). */
size_t list_length(const struct list_node *head);
int    list_contains(const struct list_node *head, const struct list_node *n);
/* Validate next/prev consistency for every node; returns 1 if intact. */
int    list_validate(const struct list_node *head);
/* Insertion sort by comparator (cmp(a,b) < 0 => a before b). Stable. */
void   list_sort(struct list_node *head,
                 int (*cmp)(const struct list_node *, const struct list_node *));

#endif /* MYOS_LIB_LIST_H */
