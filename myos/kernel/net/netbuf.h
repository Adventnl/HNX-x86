/* Packet buffer for the network stack.
 *
 * A netbuf owns one contiguous data region allocated from kmem. Protocol layers
 * grow the packet downward (push, toward lower addresses / outer headers) and
 * consume it from the front (pull, toward higher addresses / inner payload),
 * exactly like an skb/mbuf. `head` is the start of the backing region, `data`
 * the start of the live bytes, `len` the number of live bytes and `end` the
 * first byte past the region. Headroom is `data - head`; tailroom is
 * `end - (data + len)`.
 */
#ifndef MYOS_NET_NETBUF_H
#define MYOS_NET_NETBUF_H

#include "types.h"

#define NETBUF_DEFAULT_HEADROOM 64u   /* room for eth+ip+tcp/udp headers */

struct netbuf {
    uint8_t *head;     /* start of the backing allocation        */
    uint8_t *data;     /* start of the valid packet bytes        */
    uint8_t *tail;     /* one past the last valid packet byte    */
    uint8_t *end;      /* one past the backing allocation        */
    size_t   alloc;    /* total backing bytes (end - head)       */
    /* Layer pointers stamped during parsing/building (optional). */
    uint8_t *eth;
    uint8_t *net;      /* network (IPv4) header                  */
    uint8_t *xport;    /* transport (TCP/UDP/ICMP) header        */
    void    *netif;    /* owning interface, when relevant        */
};

/* Allocate a netbuf able to hold `capacity` payload bytes plus the default
 * headroom. Returns NULL on allocation failure. */
struct netbuf *netbuf_alloc(size_t capacity);

/* Allocate with an explicit headroom reservation. */
struct netbuf *netbuf_alloc_headroom(size_t capacity, size_t headroom);

void netbuf_free(struct netbuf *nb);

/* Current live length / available space. */
static inline size_t netbuf_len(const struct netbuf *nb) {
    return (size_t)(nb->tail - nb->data);
}
static inline size_t netbuf_headroom(const struct netbuf *nb) {
    return (size_t)(nb->data - nb->head);
}
static inline size_t netbuf_tailroom(const struct netbuf *nb) {
    return (size_t)(nb->end - nb->tail);
}

/* Reserve headroom on an empty buffer (moves data/tail forward). Returns 0 on
 * success, -1 if there is not enough room. */
int netbuf_reserve(struct netbuf *nb, size_t len);

/* Prepend `len` bytes of header space; returns a pointer to the new start of
 * data (the freshly exposed bytes) or NULL if there is no headroom. */
uint8_t *netbuf_push(struct netbuf *nb, size_t len);

/* Remove `len` bytes from the front; returns the new data pointer or NULL if
 * the buffer is shorter than `len`. */
uint8_t *netbuf_pull(struct netbuf *nb, size_t len);

/* Append `len` bytes at the tail; returns a pointer to the first appended byte
 * or NULL if there is no tailroom. The bytes are left uninitialized. */
uint8_t *netbuf_put(struct netbuf *nb, size_t len);

/* Trim the buffer down to exactly `len` live bytes (no-op if already shorter). */
void netbuf_trim(struct netbuf *nb, size_t len);

/* Reset to empty with the given headroom (re-uses the backing allocation). */
void netbuf_reset(struct netbuf *nb, size_t headroom);

/* Copy raw bytes into a freshly put region; convenience for building. Returns
 * the destination pointer or NULL on no tailroom. */
uint8_t *netbuf_put_data(struct netbuf *nb, const void *src, size_t len);

#endif /* MYOS_NET_NETBUF_H */
