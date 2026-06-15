/* xHCI root hub scan + device addressing (see xhci_roothub.h). */
#include "xhci_roothub.h"
#include "xhci_port.h"
#include "xhci_command.h"
#include "xhci_context.h"
#include "xhci_ring.h"
#include "log.h"

uint16_t xhci_ep0_max_packet(uint8_t speed) {
    switch (speed) {
    case XHCI_SPEED_SUPER: return 512;
    case XHCI_SPEED_HIGH:  return 64;
    case XHCI_SPEED_LOW:
    case XHCI_SPEED_FULL:
    default:               return 8;
    }
}

int xhci_address_port_device(struct xhci *xhc, uint8_t port, uint8_t *speed_out) {
    if (xhci_port_reset(xhc, port) != 0) {
        return 0;
    }
    uint8_t speed = xhci_port_speed(xhc, port);
    if (speed_out) {
        *speed_out = speed;
    }

    /* Enable a device slot. */
    uint8_t slot = 0;
    if (xhci_cmd_enable_slot(xhc, &slot) != XHCI_CC_SUCCESS || slot == 0 ||
        slot >= XHCI_MAX_SLOTS_CAP) {
        return 0;
    }

    /* Device context (controller-owned) + EP0 transfer ring (driver-owned). */
    void *devctx = xhci_alloc_context_block(xhc);
    struct xhci_trb *ep0 = xhci_ring_alloc();
    void *inctx = xhci_alloc_context_block(xhc);
    if (!devctx || !ep0 || !inctx) {
        return 0;
    }
    xhc->dev_context[slot] = devctx;
    xhc->dcbaa[slot] = (uint64_t)(uintptr_t)devctx;
    xhc->ep0_ring[slot] = ep0;
    xhc->ep0_cycle[slot] = 1;
    xhc->ep0_enqueue[slot] = 0;

    /* Build the input context: enable Slot (A0) + EP0 (A1). */
    uint32_t *icc  = xhci_ctx_dword(xhc, inctx, 0);   /* input control context */
    uint32_t *slotc = xhci_ctx_dword(xhc, inctx, 1);  /* slot context          */
    uint32_t *ep0c  = xhci_ctx_dword(xhc, inctx, 2);  /* EP0 (DCI 1) context   */

    icc[0] = 0;                                       /* drop flags  */
    icc[1] = 0x3;                                     /* add A0 | A1 */

    slotc[0] = ((uint32_t)speed << 20) | (1u << 27);  /* speed, context entries=1 */
    slotc[1] = ((uint32_t)port << 16);                /* root hub port number     */

    uint16_t mps = xhci_ep0_max_packet(speed);
    uint64_t ep0_phys = (uint64_t)(uintptr_t)ep0;
    ep0c[0] = 0;
    ep0c[1] = (3u << 1) | (4u << 3) | ((uint32_t)mps << 16); /* CErr=3, Control, MPS */
    ep0c[2] = (uint32_t)(ep0_phys | 1);               /* TR dequeue lo | DCS=1 */
    ep0c[3] = (uint32_t)(ep0_phys >> 32);
    ep0c[4] = 8;                                      /* average TRB length */

    if (xhci_cmd_address_device(xhc, slot, (uint64_t)(uintptr_t)inctx) != XHCI_CC_SUCCESS) {
        return 0;
    }
    return slot;
}

void xhci_scan_root_hub(struct xhci *xhc) {
    int connected = 0;
    for (uint8_t p = 1; p <= xhc->max_ports; p++) {
        uint32_t sc = xhci_port_read(xhc, p);
        if (sc & XHCI_PORTSC_CCS) {
            connected++;
            kernel_log("    xhci port ");
            kernel_log_hex64("up: ", p);
            kernel_log("      ");
            kernel_log_line(xhci_speed_name((uint8_t)XHCI_PORTSC_SPEED(sc)));
        }
    }
    xhc->ports_connected = connected;
    kernel_log_hex64("    xhci ports up  : ", (uint64_t)connected);
    kernel_log_ok("xHCI root hub scanned");
}
