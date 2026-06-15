/* xHCI controller bring-up: map MMIO, parse capabilities, reset, allocate the
 * DCBAA / command ring / event ring / scratchpad, and start the controller. */
#ifndef MYOS_XHCI_CONTROLLER_H
#define MYOS_XHCI_CONTROLLER_H

#include "types.h"
#include "xhci.h"

/* Full bring-up of `xhc` (its ->pci must already be set). Emits the MMIO /
 * capability / command-ring / event-ring / started markers. Returns 0 on
 * success, negative on failure. */
int xhci_controller_setup(struct xhci *xhc);

#endif /* MYOS_XHCI_CONTROLLER_H */
