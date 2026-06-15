/* xHCI command ring operations (see xhci_command.h). */
#include "xhci_command.h"
#include "xhci_ring.h"
#include "xhci_event.h"

int xhci_command_submit(struct xhci *xhc, uint64_t parameter, uint32_t control,
                        uint8_t *slot_out) {
    xhci_ring_enqueue(xhc->cmd_ring, &xhc->cmd_enqueue, &xhc->cmd_cycle,
                      parameter, 0, control);
    /* Ring the command doorbell (doorbell 0, target 0). */
    xhc->db[0] = 0;

    struct xhci_trb ev;
    if (!xhci_event_poll(xhc, XHCI_TRB_CMD_COMPLETION, &ev)) {
        return -1;
    }
    if (slot_out) {
        *slot_out = (uint8_t)XHCI_EVENT_SLOT(ev.control);
    }
    return (int)XHCI_CC(ev.status);
}

int xhci_cmd_noop(struct xhci *xhc) {
    return xhci_command_submit(xhc, 0, XHCI_TRB_TYPE(XHCI_TRB_NOOP_CMD), NULL);
}

int xhci_cmd_enable_slot(struct xhci *xhc, uint8_t *slot_out) {
    return xhci_command_submit(xhc, 0, XHCI_TRB_TYPE(XHCI_TRB_ENABLE_SLOT), slot_out);
}

int xhci_cmd_address_device(struct xhci *xhc, uint8_t slot, uint64_t input_ctx_phys) {
    uint32_t control = XHCI_TRB_TYPE(XHCI_TRB_ADDRESS_DEVICE) | ((uint32_t)slot << 24);
    return xhci_command_submit(xhc, input_ctx_phys, control, NULL);
}

int xhci_cmd_configure_endpoint(struct xhci *xhc, uint8_t slot, uint64_t input_ctx_phys) {
    uint32_t control = XHCI_TRB_TYPE(XHCI_TRB_CONFIG_EP) | ((uint32_t)slot << 24);
    return xhci_command_submit(xhc, input_ctx_phys, control, NULL);
}
