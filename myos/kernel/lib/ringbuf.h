/* Byte ring buffer (single-producer / single-consumer friendly).
 *
 * Storage is caller-owned. Capacity is whatever the caller passes; the buffer
 * keeps head/tail indices and a count so a full buffer is distinguishable from
 * an empty one without wasting a slot.
 */
#ifndef MYOS_LIB_RINGBUF_H
#define MYOS_LIB_RINGBUF_H

#include "types.h"

struct ringbuf {
    uint8_t *data;
    size_t   capacity;
    size_t   head;   /* next write index */
    size_t   tail;   /* next read index  */
    size_t   count;  /* bytes currently stored */
};

static inline void ringbuf_init(struct ringbuf *r, uint8_t *data, size_t cap) {
    r->data = data;
    r->capacity = cap;
    r->head = r->tail = r->count = 0;
}

static inline int    ringbuf_empty(const struct ringbuf *r) { return r->count == 0; }
static inline int    ringbuf_full(const struct ringbuf *r)  { return r->count == r->capacity; }
static inline size_t ringbuf_used(const struct ringbuf *r)  { return r->count; }
static inline size_t ringbuf_free(const struct ringbuf *r)  { return r->capacity - r->count; }

/* Push one byte; returns 1 on success, 0 if full. */
int ringbuf_putc(struct ringbuf *r, uint8_t c);
/* Pop one byte into *out; returns 1 on success, 0 if empty. */
int ringbuf_getc(struct ringbuf *r, uint8_t *out);
/* Peek the next byte without removing it; returns 1 on success, 0 if empty. */
int ringbuf_peek(const struct ringbuf *r, uint8_t *out);

/* Bulk transfer; returns the number of bytes actually written/read. */
size_t ringbuf_write(struct ringbuf *r, const uint8_t *src, size_t n);
size_t ringbuf_read(struct ringbuf *r, uint8_t *dst, size_t n);

/* Overwriting push: when full, drops the oldest byte to make room. Always
 * succeeds. Returns 1 if a byte was dropped. */
int ringbuf_force_putc(struct ringbuf *r, uint8_t c);

void ringbuf_reset(struct ringbuf *r);

#endif /* MYOS_LIB_RINGBUF_H */
