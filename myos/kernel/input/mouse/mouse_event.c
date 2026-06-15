/* Mouse event ring (see mouse_event.h). */
#include "mouse_event.h"
#include "irq.h"
#include "string.h"

#define MOUSE_RING 64

static struct mouse_event g_ring[MOUSE_RING];
static int g_head, g_tail, g_count;

void mouse_event_init(void) {
    uint64_t flags = irq_save_flags_and_disable();
    g_head = g_tail = g_count = 0;
    memset(g_ring, 0, sizeof(g_ring));
    irq_restore_flags(flags);
}

int mouse_event_push(const struct mouse_event *ev) {
    if (!ev) {
        return -1;
    }
    uint64_t flags = irq_save_flags_and_disable();
    if (g_count >= MOUSE_RING) {
        irq_restore_flags(flags);
        return -1;
    }
    g_ring[g_head] = *ev;
    g_head = (g_head + 1) % MOUSE_RING;
    g_count++;
    irq_restore_flags(flags);
    return 0;
}

int mouse_event_pop(struct mouse_event *out) {
    uint64_t flags = irq_save_flags_and_disable();
    if (g_count == 0) {
        irq_restore_flags(flags);
        return -1;
    }
    if (out) {
        *out = g_ring[g_tail];
    }
    g_tail = (g_tail + 1) % MOUSE_RING;
    g_count--;
    irq_restore_flags(flags);
    return 0;
}

int mouse_event_count(void) {
    return g_count;
}
