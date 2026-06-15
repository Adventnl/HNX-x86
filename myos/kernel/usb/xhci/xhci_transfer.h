/* xHCI transfers: EP0 control transfers (Setup/Data/Status TDs) and interrupt-IN
 * endpoint queuing. Payloads pass through the controller's DMA bounce page so
 * callers may use ordinary (heap) buffers. */
#ifndef MYOS_XHCI_TRANSFER_H
#define MYOS_XHCI_TRANSFER_H

#include "types.h"
#include "xhci.h"

/* Standard control transfer on EP0 of `slot`. `data` may be NULL when
 * wLength == 0. Returns 0 on success, negative on failure/timeout. */
int xhci_control_transfer(struct xhci *xhc, uint8_t slot,
                          uint8_t bmRequestType, uint8_t bRequest,
                          uint16_t wValue, uint16_t wIndex,
                          uint16_t wLength, void *data);

/* Queue a Normal TRB on the slot's interrupt-IN ring (DCI in xhc->intr_dci[slot])
 * to receive up to `length` bytes into `dma_buf` (must be identity-mapped), and
 * ring the endpoint doorbell. Non-blocking. Returns 0 on success. */
int xhci_interrupt_queue(struct xhci *xhc, uint8_t slot, uint64_t dma_buf, uint16_t length);

#endif /* MYOS_XHCI_TRANSFER_H */
