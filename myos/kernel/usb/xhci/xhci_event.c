/* xHCI event ring consumer (see xhci_event.h). */
#include "xhci_event.h"

#define XHCI_EVENT_SPIN 2000000u

static void cpu_relax(void) {
    __asm__ volatile("pause");
}

/* Consume the event at the dequeue pointer if its cycle bit matches the consumer
 * cycle. Returns 1 and copies it to *out, else 0. Advances ERDP. */
static int consume_one(struct xhci *xhc, struct xhci_trb *out) {
    struct xhci_trb *e = &xhc->event_ring[xhc->event_dequeue];
    uint8_t cycle = (uint8_t)(e->control & XHCI_TRB_CYCLE);
    if (cycle != xhc->event_cycle) {
        return 0;                       /* software owns nothing yet */
    }
    *out = *e;                          /* copy before advancing */

    xhc->event_dequeue++;
    if (xhc->event_dequeue >= XHCI_RING_TRBS) {
        xhc->event_dequeue = 0;
        xhc->event_cycle ^= 1;
    }
    uint64_t erdp = (uint64_t)(uintptr_t)&xhc->event_ring[xhc->event_dequeue] | XHCI_ERDP_EHB;
    xhci_write64(xhc->rt, XHCI_RT_IR0 + XHCI_IR_ERDP, erdp);
    return 1;
}

int xhci_event_poll(struct xhci *xhc, uint8_t want_type, struct xhci_trb *out) {
    struct xhci_trb ev;
    for (uint32_t spin = 0; spin < XHCI_EVENT_SPIN; spin++) {
        if (consume_one(xhc, &ev)) {
            uint8_t t = XHCI_TRB_GET_TYPE(ev.control);
            if (want_type == 0 || t == want_type) {
                if (out) {
                    *out = ev;
                }
                return 1;
            }
            /* Different event type: drop and keep polling. */
            continue;
        }
        cpu_relax();
    }
    return 0;
}

void xhci_event_drain(struct xhci *xhc) {
    struct xhci_trb ev;
    int guard = 0;
    while (consume_one(xhc, &ev) && guard++ < XHCI_RING_TRBS) {
        /* discard */
    }
}

int xhci_event_try(struct xhci *xhc, struct xhci_trb *out) {
    return consume_one(xhc, out);
}
