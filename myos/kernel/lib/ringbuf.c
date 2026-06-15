/* Byte ring buffer implementation (see kernel/lib/ringbuf.h). */
#include "ringbuf.h"

int ringbuf_putc(struct ringbuf *r, uint8_t c) {
    if (ringbuf_full(r)) {
        return 0;
    }
    r->data[r->head] = c;
    r->head = (r->head + 1) % r->capacity;
    r->count++;
    return 1;
}

int ringbuf_getc(struct ringbuf *r, uint8_t *out) {
    if (ringbuf_empty(r)) {
        return 0;
    }
    *out = r->data[r->tail];
    r->tail = (r->tail + 1) % r->capacity;
    r->count--;
    return 1;
}

int ringbuf_peek(const struct ringbuf *r, uint8_t *out) {
    if (ringbuf_empty(r)) {
        return 0;
    }
    *out = r->data[r->tail];
    return 1;
}

size_t ringbuf_write(struct ringbuf *r, const uint8_t *src, size_t n) {
    size_t written = 0;
    while (written < n && !ringbuf_full(r)) {
        r->data[r->head] = src[written];
        r->head = (r->head + 1) % r->capacity;
        r->count++;
        written++;
    }
    return written;
}

size_t ringbuf_read(struct ringbuf *r, uint8_t *dst, size_t n) {
    size_t got = 0;
    while (got < n && !ringbuf_empty(r)) {
        dst[got] = r->data[r->tail];
        r->tail = (r->tail + 1) % r->capacity;
        r->count--;
        got++;
    }
    return got;
}

int ringbuf_force_putc(struct ringbuf *r, uint8_t c) {
    int dropped = 0;
    if (ringbuf_full(r)) {
        /* Drop the oldest byte. */
        r->tail = (r->tail + 1) % r->capacity;
        r->count--;
        dropped = 1;
    }
    r->data[r->head] = c;
    r->head = (r->head + 1) % r->capacity;
    r->count++;
    return dropped;
}

void ringbuf_reset(struct ringbuf *r) {
    r->head = r->tail = r->count = 0;
}
