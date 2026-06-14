/* Input event ring. Producers run in IRQ context, so push/pop guard with IF. */
#include "input_queue.h"
#include "irq.h"

#define QUEUE_SIZE 128

static struct input_event g_ring[QUEUE_SIZE];
static int g_head, g_tail, g_count;

void input_queue_init(void) {
    g_head = g_tail = g_count = 0;
}

int input_queue_push(const struct input_event *ev) {
    uint64_t f = irq_save_flags_and_disable();
    if (g_count >= QUEUE_SIZE) {
        irq_restore_flags(f);
        return -1;
    }
    g_ring[g_tail] = *ev;
    g_tail = (g_tail + 1) % QUEUE_SIZE;
    g_count++;
    irq_restore_flags(f);
    return 0;
}

int input_queue_pop(struct input_event *out) {
    uint64_t f = irq_save_flags_and_disable();
    if (g_count == 0) {
        irq_restore_flags(f);
        return -1;
    }
    *out = g_ring[g_head];
    g_head = (g_head + 1) % QUEUE_SIZE;
    g_count--;
    irq_restore_flags(f);
    return 0;
}

int input_queue_count(void) {
    return g_count;
}
