/* xHCI root-hub port register access (1-based port numbers). */
#ifndef MYOS_XHCI_PORT_H
#define MYOS_XHCI_PORT_H

#include "types.h"
#include "xhci.h"

uint32_t xhci_port_read(struct xhci *xhc, uint8_t port);
int      xhci_port_connected(struct xhci *xhc, uint8_t port);
uint8_t  xhci_port_speed(struct xhci *xhc, uint8_t port);
const char *xhci_speed_name(uint8_t speed);

/* Reset a connected port and wait for it to enable. Returns 0 on success. */
int      xhci_port_reset(struct xhci *xhc, uint8_t port);

#endif /* MYOS_XHCI_PORT_H */
