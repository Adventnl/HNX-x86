/* xHCI root-hub port handling (see xhci_port.h). */
#include "xhci_port.h"

#define PORT_SPIN 2000000u

static uint32_t port_off(uint8_t port) {
    return XHCI_OP_PORTS + (uint32_t)(port - 1) * XHCI_PORT_STRIDE + XHCI_PORT_PORTSC;
}

/* Strip the write-1-to-clear change bits and the PED bit so a read-modify-write
 * does not accidentally clear status or disable the port. */
static uint32_t port_preserve(uint32_t sc) {
    return sc & ~(XHCI_PORTSC_RW1CS | XHCI_PORTSC_PED);
}

uint32_t xhci_port_read(struct xhci *xhc, uint8_t port) {
    return xhci_read32(xhc->op, port_off(port));
}

int xhci_port_connected(struct xhci *xhc, uint8_t port) {
    return (xhci_port_read(xhc, port) & XHCI_PORTSC_CCS) ? 1 : 0;
}

uint8_t xhci_port_speed(struct xhci *xhc, uint8_t port) {
    return (uint8_t)XHCI_PORTSC_SPEED(xhci_port_read(xhc, port));
}

const char *xhci_speed_name(uint8_t speed) {
    switch (speed) {
    case XHCI_SPEED_FULL:  return "full-speed";
    case XHCI_SPEED_LOW:   return "low-speed";
    case XHCI_SPEED_HIGH:  return "high-speed";
    case XHCI_SPEED_SUPER: return "super-speed";
    default:               return "unknown-speed";
    }
}

int xhci_port_reset(struct xhci *xhc, uint8_t port) {
    uint32_t sc = xhci_port_read(xhc, port);
    if (!(sc & XHCI_PORTSC_CCS)) {
        return -1;
    }
    /* Assert port reset. */
    xhci_write32(xhc->op, port_off(port), port_preserve(sc) | XHCI_PORTSC_PR);

    /* Wait for the reset to complete (PRC) or the port to enable. */
    for (uint32_t spin = 0; spin < PORT_SPIN; spin++) {
        sc = xhci_port_read(xhc, port);
        if (sc & XHCI_PORTSC_PRC) {
            break;
        }
        __asm__ volatile("pause");
    }
    /* Acknowledge the reset-change bit. */
    sc = xhci_port_read(xhc, port);
    xhci_write32(xhc->op, port_off(port), port_preserve(sc) | XHCI_PORTSC_PRC);

    /* A successful reset leaves the port enabled. */
    sc = xhci_port_read(xhc, port);
    return (sc & XHCI_PORTSC_PED) ? 0 : -1;
}
