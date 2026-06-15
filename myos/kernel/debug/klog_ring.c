/* Kernel log ring buffer implementation (see kernel/debug/klog_ring.h). */
#include "klog_ring.h"
#include "ringbuf.h"
#include "log.h"
#include "string.h"

static uint8_t        g_storage[KLOG_RING_SIZE];
static struct ringbuf g_ring;
static uint64_t       g_total;
static uint64_t       g_dropped;
static int            g_inited;

void klog_ring_init(void) {
    ringbuf_init(&g_ring, g_storage, KLOG_RING_SIZE);
    g_total = 0;
    g_dropped = 0;
    g_inited = 1;
}

void klog_ring_write_n(const char *s, size_t n) {
    if (!g_inited) {
        klog_ring_init();
    }
    for (size_t i = 0; i < n; i++) {
        if (ringbuf_force_putc(&g_ring, (uint8_t)s[i])) {
            g_dropped++;
        }
        g_total++;
    }
}

void klog_ring_write(const char *s) {
    klog_ring_write_n(s, strlen(s));
}

size_t klog_ring_snapshot(char *out, size_t max) {
    if (!g_inited) {
        return 0;
    }
    size_t used = ringbuf_used(&g_ring);
    size_t skip = used > max ? used - max : 0;
    size_t copied = 0;
    /* Walk the ring without consuming it. */
    size_t idx = g_ring.tail;
    for (size_t i = 0; i < used; i++) {
        uint8_t c = g_ring.data[idx];
        idx = (idx + 1) % g_ring.capacity;
        if (i < skip) {
            continue;
        }
        if (copied < max) {
            out[copied++] = (char)c;
        }
    }
    return copied;
}

size_t klog_ring_used(void) {
    return g_inited ? ringbuf_used(&g_ring) : 0;
}

uint64_t klog_ring_total_bytes(void) { return g_total; }
uint64_t klog_ring_dropped(void) { return g_dropped; }

void klog_ring_dump(void) {
    static char buf[KLOG_RING_SIZE + 1];
    size_t n = klog_ring_snapshot(buf, KLOG_RING_SIZE);
    buf[n] = '\0';
    kernel_log_line("---- kernel log ring (dmesg) ----");
    kernel_log(buf);
    kernel_log_line("---- end log ring ----");
}
