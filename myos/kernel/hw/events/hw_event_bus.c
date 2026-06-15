/* Hardware event bus implementation (see hw_event_bus.h). */
#include "hw_event_bus.h"
#include "irq.h"
#include "string.h"
#include "log.h"

#define HW_EVENT_RING 64

static struct hw_event g_ring[HW_EVENT_RING];
static uint32_t        g_head;          /* next write slot                 */
static uint64_t        g_total;         /* lifetime event count            */
static int             g_ready;

void hw_event_bus_init(void) {
    uint64_t flags = irq_save_flags_and_disable();
    g_head = 0;
    g_total = 0;
    memset(g_ring, 0, sizeof(g_ring));
    g_ready = 1;
    irq_restore_flags(flags);
    kernel_log_ok("Hardware event bus online");
}

void hw_event_emit(enum hw_event_type type, uint64_t a, uint64_t b, const char *message) {
    if (!g_ready) {
        return;
    }
    uint64_t flags = irq_save_flags_and_disable();
    struct hw_event *e = &g_ring[g_head % HW_EVENT_RING];
    e->type = (uint32_t)type;
    e->seq = (uint32_t)g_total;
    e->a = a;
    e->b = b;
    if (message) {
        strlcpy(e->message, message, sizeof(e->message));
    } else {
        e->message[0] = 0;
    }
    g_head++;
    g_total++;
    irq_restore_flags(flags);
}

uint64_t hw_event_count(void) {
    return g_total;
}

int hw_event_recent(struct hw_event *out, int max) {
    if (!out || max <= 0) {
        return 0;
    }
    uint64_t flags = irq_save_flags_and_disable();
    int have = (g_total < HW_EVENT_RING) ? (int)g_total : HW_EVENT_RING;
    if (max > have) {
        max = have;
    }
    /* Oldest of the returned window first. */
    for (int i = 0; i < max; i++) {
        uint32_t idx = (g_head - (uint32_t)max + (uint32_t)i) % HW_EVENT_RING;
        out[i] = g_ring[idx];
    }
    irq_restore_flags(flags);
    return max;
}

void hw_event_dump_recent(void) {
    struct hw_event tmp[8];
    int n = hw_event_recent(tmp, 8);
    kernel_log_hex64("    hw events total : ", g_total);
    for (int i = 0; i < n; i++) {
        kernel_log("    evt ");
        kernel_log(hw_event_type_name((enum hw_event_type)tmp[i].type));
        kernel_log("  ");
        kernel_log_line(tmp[i].message);
    }
}
