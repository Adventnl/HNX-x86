/* xHCI TRB ring helpers (producer side). A ring is one DMA page of 256 TRBs;
 * the last TRB is a Link TRB back to the start with the Toggle-Cycle bit set, so
 * 255 usable slots per ring. */
#ifndef MYOS_XHCI_RING_H
#define MYOS_XHCI_RING_H

#include "types.h"
#include "xhci_regs.h"

/* Allocate + initialise a transfer/command ring. Returns the ring (virt==phys)
 * or NULL. The Link TRB is installed in the last slot. */
struct xhci_trb *xhci_ring_alloc(void);

/* Enqueue one TRB (cycle bit applied from *cycle). Handles the link-TRB wrap +
 * cycle toggle. Returns the physical address of the enqueued TRB. */
uint64_t xhci_ring_enqueue(struct xhci_trb *ring, uint32_t *enqueue, uint8_t *cycle,
                           uint64_t parameter, uint32_t status, uint32_t control);

#endif /* MYOS_XHCI_RING_H */
