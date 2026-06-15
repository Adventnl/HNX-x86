/* xHCI command ring operations. Each call posts a command TRB, rings the command
 * doorbell and polls for the Command Completion Event. Returns the completion
 * code (XHCI_CC_SUCCESS == 1) or a negative value on timeout. */
#ifndef MYOS_XHCI_COMMAND_H
#define MYOS_XHCI_COMMAND_H

#include "types.h"
#include "xhci.h"

int xhci_command_submit(struct xhci *xhc, uint64_t parameter, uint32_t control,
                        uint8_t *slot_out);

int xhci_cmd_noop(struct xhci *xhc);
int xhci_cmd_enable_slot(struct xhci *xhc, uint8_t *slot_out);
int xhci_cmd_address_device(struct xhci *xhc, uint8_t slot, uint64_t input_ctx_phys);
int xhci_cmd_configure_endpoint(struct xhci *xhc, uint8_t slot, uint64_t input_ctx_phys);

#endif /* MYOS_XHCI_COMMAND_H */
