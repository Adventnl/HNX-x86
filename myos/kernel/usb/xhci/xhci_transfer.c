/* xHCI transfers (see xhci_transfer.h). */
#include "xhci_transfer.h"
#include "xhci_ring.h"
#include "xhci_event.h"
#include "memory_layout.h"
#include "string.h"

int xhci_control_transfer(struct xhci *xhc, uint8_t slot,
                          uint8_t bmRequestType, uint8_t bRequest,
                          uint16_t wValue, uint16_t wIndex,
                          uint16_t wLength, void *data) {
    struct xhci_trb *ring = xhc->ep0_ring[slot];
    if (!ring) {
        return -1;
    }
    int dir_in = (bmRequestType & 0x80) != 0;
    if (wLength > PAGE_SIZE) {
        wLength = (uint16_t)PAGE_SIZE;
    }
    void *bounce = (void *)(uintptr_t)xhc->bounce_phys;
    if (wLength && !dir_in && data) {
        memcpy(bounce, data, wLength);     /* OUT: stage caller data */
    }

    uint32_t *enq = &xhc->ep0_enqueue[slot];
    uint8_t  *cyc = &xhc->ep0_cycle[slot];

    /* Setup stage (immediate data). */
    uint64_t setup = (uint64_t)bmRequestType |
                     ((uint64_t)bRequest << 8) |
                     ((uint64_t)wValue   << 16) |
                     ((uint64_t)wIndex   << 32) |
                     ((uint64_t)wLength  << 48);
    uint32_t trt = (wLength == 0) ? XHCI_TRT_NO_DATA : (dir_in ? XHCI_TRT_IN : XHCI_TRT_OUT);
    xhci_ring_enqueue(ring, enq, cyc, setup, 8,
                      XHCI_TRB_TYPE(XHCI_TRB_SETUP) | XHCI_TRB_IDT | (trt << 16));

    /* Data stage. */
    if (wLength > 0) {
        xhci_ring_enqueue(ring, enq, cyc, xhc->bounce_phys, wLength,
                          XHCI_TRB_TYPE(XHCI_TRB_DATA) | (dir_in ? XHCI_TRB_DIR_IN : 0));
    }

    /* Status stage (opposite direction, interrupt on completion). */
    int status_in = (wLength == 0) ? 1 : !dir_in;
    xhci_ring_enqueue(ring, enq, cyc, 0, 0,
                      XHCI_TRB_TYPE(XHCI_TRB_STATUS) |
                      (status_in ? XHCI_TRB_DIR_IN : 0) | XHCI_TRB_IOC);

    /* Ring the slot doorbell, target EP0 (DCI 1). */
    xhc->db[slot] = 1;

    struct xhci_trb ev;
    if (!xhci_event_poll(xhc, XHCI_TRB_TRANSFER_EVENT, &ev)) {
        return -1;
    }
    if (XHCI_CC(ev.status) != XHCI_CC_SUCCESS) {
        return -(int)XHCI_CC(ev.status);
    }
    if (wLength && dir_in && data) {
        memcpy(data, bounce, wLength);     /* IN: copy result to caller */
    }
    return 0;
}

int xhci_interrupt_queue(struct xhci *xhc, uint8_t slot, uint64_t dma_buf, uint16_t length) {
    struct xhci_trb *ring = xhc->intr_ring[slot];
    uint8_t dci = xhc->intr_dci[slot];
    if (!ring || !dci) {
        return -1;
    }
    xhci_ring_enqueue(ring, &xhc->intr_enqueue[slot], &xhc->intr_cycle[slot],
                      dma_buf, length, XHCI_TRB_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC);
    xhc->db[slot] = dci;                    /* ring endpoint doorbell */
    return 0;
}
