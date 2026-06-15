/* xHCI root hub: scan ports and address connected devices.
 *
 * xhci_address_port_device() performs the port reset -> Enable Slot -> context
 * build -> Address Device sequence, leaving the slot ready for EP0 control
 * transfers. The USB core then fetches descriptors over EP0. */
#ifndef MYOS_XHCI_ROOTHUB_H
#define MYOS_XHCI_ROOTHUB_H

#include "types.h"
#include "xhci.h"

/* Reset + address the device on root port `port` (1-based). Returns the slot id
 * (1..max_slots) on success, 0 on failure. *speed_out gets the link speed. */
int xhci_address_port_device(struct xhci *xhc, uint8_t port, uint8_t *speed_out);

/* Max-packet-size for EP0 implied by the link speed. */
uint16_t xhci_ep0_max_packet(uint8_t speed);

#endif /* MYOS_XHCI_ROOTHUB_H */
