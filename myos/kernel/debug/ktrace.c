/* Kernel tracing implementation (see kernel/debug/ktrace.h). */
#include "ktrace.h"
#include "timer.h"
#include "log.h"
#include "fmt.h"

static struct ktrace_event g_events[KTRACE_RING_EVENTS];
static uint64_t            g_head;       /* next write index (monotonic) */
static uint64_t            g_seq;
static uint32_t            g_mask;
static int                 g_inited;

void ktrace_init(void) {
    g_head = 0;
    g_seq = 0;
    g_mask = 0;
    g_inited = 1;
}

void ktrace_enable(enum ktrace_cat cat)  { g_mask |= (1u << cat); }
void ktrace_disable(enum ktrace_cat cat) { g_mask &= ~(1u << cat); }
void ktrace_enable_all(void)  { g_mask = ~0u; }
void ktrace_disable_all(void) { g_mask = 0; }

int ktrace_enabled(enum ktrace_cat cat) {
    return g_inited && (g_mask & (1u << cat)) != 0;
}

static uint64_t now_tick(void) {
    return kernel_ticks();
}

void ktrace_emit(enum ktrace_cat cat, uint32_t id, const char *msg,
                 uint64_t arg0, uint64_t arg1) {
    if (!g_inited) {
        ktrace_init();
    }
    size_t slot = (size_t)(g_head % KTRACE_RING_EVENTS);
    struct ktrace_event *e = &g_events[slot];
    e->seq = g_seq++;
    e->tick = now_tick();
    e->cat = cat;
    e->id = id;
    e->arg0 = arg0;
    e->arg1 = arg1;
    e->msg = msg;
    g_head++;
}

uint64_t ktrace_count(void) {
    return g_seq;
}

size_t ktrace_snapshot(struct ktrace_event *out, size_t max) {
    uint64_t total = g_seq < KTRACE_RING_EVENTS ? g_seq : KTRACE_RING_EVENTS;
    size_t n = total < max ? (size_t)total : max;
    /* Most-recent first. */
    for (size_t i = 0; i < n; i++) {
        uint64_t idx = (g_head - 1 - i);
        out[i] = g_events[idx % KTRACE_RING_EVENTS];
    }
    return n;
}

const char *ktrace_cat_name(enum ktrace_cat cat) {
    switch (cat) {
    case KTRACE_CAT_SCHED:   return "sched";
    case KTRACE_CAT_MEM:     return "mem";
    case KTRACE_CAT_IRQ:     return "irq";
    case KTRACE_CAT_FS:      return "fs";
    case KTRACE_CAT_BLOCK:   return "block";
    case KTRACE_CAT_NET:     return "net";
    case KTRACE_CAT_USB:     return "usb";
    case KTRACE_CAT_SYSCALL: return "syscall";
    case KTRACE_CAT_DRIVER:  return "driver";
    default:                 return "?";
    }
}

void ktrace_dump(size_t max) {
    static struct ktrace_event snap[64];
    if (max > 64) {
        max = 64;
    }
    size_t n = ktrace_snapshot(snap, max);
    kdprintf("---- ktrace (%u of %u events) ----\n", (unsigned)n,
             (unsigned)ktrace_count());
    for (size_t i = 0; i < n; i++) {
        struct ktrace_event *e = &snap[i];
        kdprintf("  [%u] t=%u %s id=%u %s a0=0x%x a1=0x%x\n",
                 (unsigned)e->seq, (unsigned)e->tick, ktrace_cat_name(e->cat),
                 (unsigned)e->id, e->msg ? e->msg : "", (unsigned)e->arg0,
                 (unsigned)e->arg1);
    }
}
