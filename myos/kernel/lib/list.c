/* Out-of-line helpers for the intrusive list (kernel/lib/list.h). */
#include "list.h"

size_t list_length(const struct list_node *head) {
    size_t count = 0;
    const struct list_node *p = head->next;
    while (p != head) {
        count++;
        p = p->next;
    }
    return count;
}

int list_contains(const struct list_node *head, const struct list_node *n) {
    const struct list_node *p;
    list_for_each(p, head) {
        if (p == n) {
            return 1;
        }
    }
    return 0;
}

int list_validate(const struct list_node *head) {
    const struct list_node *p = head;
    /* Walk forward; every node's next->prev must point back at it. A corrupt
     * list would loop forever, so bound the walk generously. */
    size_t guard = 0;
    do {
        if (p->next->prev != p) {
            return 0;
        }
        if (p->prev->next != p) {
            return 0;
        }
        p = p->next;
        if (++guard > 0x1000000ULL) {
            return 0;
        }
    } while (p != head);
    return 1;
}

void list_sort(struct list_node *head,
               int (*cmp)(const struct list_node *, const struct list_node *)) {
    if (list_empty(head) || head->next->next == head) {
        return; /* 0 or 1 elements */
    }
    /* Re-insert nodes one at a time in sorted order (stable insertion sort:
     * equal keys keep their relative order). The original chain is still
     * traversable forward through `node->next` after head is re-init'd because
     * the last node's next pointer still references the (same) head object. */
    struct list_node sorted;
    list_init(&sorted);

    struct list_node *node = head->next;
    list_init(head); /* head now empty; chain reachable via node pointers */

    while (node != head) {
        struct list_node *next = node->next;
        struct list_node *pos = sorted.next;
        while (pos != &sorted && cmp(pos, node) <= 0) {
            pos = pos->next;
        }
        __list_add(node, pos->prev, pos);
        node = next;
    }
    list_splice_init(&sorted, head);
}
