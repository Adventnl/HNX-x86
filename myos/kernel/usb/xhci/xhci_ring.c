/* xHCI TRB ring producer (see xhci_ring.h). */
#include "xhci_ring.h"
#include "pmm.h"
#include "memory_layout.h"
#include "string.h"

struct xhci_trb *xhci_ring_alloc(void) {
    uint64_t page = pmm_alloc_page();
    if (page == PMM_INVALID_PAGE) {
        return NULL;
    }
    memset((void *)(uintptr_t)page, 0, PAGE_SIZE);
    struct xhci_trb *ring = (struct xhci_trb *)(uintptr_t)page;

    /* Link TRB in the last slot, pointing back to the ring base, toggling the
     * cycle state on wrap. */
    struct xhci_trb *link = &ring[XHCI_RING_TRBS - 1];
    link->parameter = page;
    link->status = 0;
    link->control = XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TOGGLE;
    return ring;
}

uint64_t xhci_ring_enqueue(struct xhci_trb *ring, uint32_t *enqueue, uint8_t *cycle,
                           uint64_t parameter, uint32_t status, uint32_t control) {
    struct xhci_trb *trb = &ring[*enqueue];
    trb->parameter = parameter;
    trb->status = status;

    uint32_t c = control & ~XHCI_TRB_CYCLE;
    if (*cycle) {
        c |= XHCI_TRB_CYCLE;
    }
    trb->control = c;
    uint64_t phys = (uint64_t)(uintptr_t)trb;

    (*enqueue)++;
    if (*enqueue >= XHCI_RING_TRBS - 1) {
        /* Reached the Link TRB: publish the current cycle into it, then wrap. */
        struct xhci_trb *link = &ring[XHCI_RING_TRBS - 1];
        if (*cycle) {
            link->control |= XHCI_TRB_CYCLE;
        } else {
            link->control &= ~XHCI_TRB_CYCLE;
        }
        *enqueue = 0;
        *cycle ^= 1;
    }
    return phys;
}
