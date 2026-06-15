/* Packet buffer implementation. */
#include "netbuf.h"
#include "slab.h"
#include "string.h"

struct netbuf *netbuf_alloc_headroom(size_t capacity, size_t headroom) {
    size_t total = capacity + headroom;
    if (total == 0) {
        total = 1;
    }
    struct netbuf *nb = (struct netbuf *)kmem_zalloc(sizeof(*nb));
    if (nb == NULL) {
        return NULL;
    }
    nb->head = (uint8_t *)kmem_alloc(total);
    if (nb->head == NULL) {
        kmem_free(nb);
        return NULL;
    }
    nb->alloc = total;
    nb->end = nb->head + total;
    nb->data = nb->head + headroom;
    nb->tail = nb->data;
    return nb;
}

struct netbuf *netbuf_alloc(size_t capacity) {
    return netbuf_alloc_headroom(capacity, NETBUF_DEFAULT_HEADROOM);
}

void netbuf_free(struct netbuf *nb) {
    if (nb == NULL) {
        return;
    }
    if (nb->head != NULL) {
        kmem_free(nb->head);
    }
    kmem_free(nb);
}

int netbuf_reserve(struct netbuf *nb, size_t len) {
    /* Only meaningful on an empty buffer. */
    if (netbuf_len(nb) != 0) {
        return -1;
    }
    if ((size_t)(nb->end - nb->data) < len) {
        return -1;
    }
    nb->data += len;
    nb->tail = nb->data;
    return 0;
}

uint8_t *netbuf_push(struct netbuf *nb, size_t len) {
    if (netbuf_headroom(nb) < len) {
        return NULL;
    }
    nb->data -= len;
    return nb->data;
}

uint8_t *netbuf_pull(struct netbuf *nb, size_t len) {
    if (netbuf_len(nb) < len) {
        return NULL;
    }
    nb->data += len;
    return nb->data;
}

uint8_t *netbuf_put(struct netbuf *nb, size_t len) {
    if (netbuf_tailroom(nb) < len) {
        return NULL;
    }
    uint8_t *p = nb->tail;
    nb->tail += len;
    return p;
}

void netbuf_trim(struct netbuf *nb, size_t len) {
    if (netbuf_len(nb) > len) {
        nb->tail = nb->data + len;
    }
}

void netbuf_reset(struct netbuf *nb, size_t headroom) {
    if (headroom > nb->alloc) {
        headroom = 0;
    }
    nb->data = nb->head + headroom;
    nb->tail = nb->data;
    nb->eth = NULL;
    nb->net = NULL;
    nb->xport = NULL;
}

uint8_t *netbuf_put_data(struct netbuf *nb, const void *src, size_t len) {
    uint8_t *dst = netbuf_put(nb, len);
    if (dst != NULL && len != 0) {
        memcpy(dst, src, len);
    }
    return dst;
}
