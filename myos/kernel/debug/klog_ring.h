/* In-memory kernel log ring buffer.
 *
 * Mirrors every kernel_log* line into a fixed byte ring so the most recent log
 * history can be dumped after a fault, exported through a /proc-style node, or
 * inspected by a userland `dmesg`. Independent of the serial/framebuffer sinks.
 */
#ifndef MYOS_DEBUG_KLOG_RING_H
#define MYOS_DEBUG_KLOG_RING_H

#include "types.h"

#define KLOG_RING_SIZE (64 * 1024)

void klog_ring_init(void);
void klog_ring_write(const char *s);       /* append raw text */
void klog_ring_write_n(const char *s, size_t n);

/* Copy up to `max` of the most recent bytes into out; returns bytes copied. */
size_t klog_ring_snapshot(char *out, size_t max);
size_t klog_ring_used(void);
uint64_t klog_ring_total_bytes(void);      /* lifetime bytes written */
uint64_t klog_ring_dropped(void);          /* bytes overwritten (lost) */

/* Print the ring contents through the live logger (dmesg). */
void klog_ring_dump(void);

#endif /* MYOS_DEBUG_KLOG_RING_H */
