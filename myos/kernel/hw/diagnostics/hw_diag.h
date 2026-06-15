/* Hardware diagnostics: a single snapshot of the device/driver/interrupt/event
 * state, consumed by the kernel log and the userland `hwinfo` tool. */
#ifndef MYOS_HW_DIAG_H
#define MYOS_HW_DIAG_H

#include "types.h"

struct hw_diag_summary {
    uint32_t pci_functions;
    uint32_t devices;
    uint32_t block_devices;
    uint32_t usb_devices;
    uint32_t irq_active_vectors;
    uint64_t irq_total;
    uint64_t hw_events;
};

void hw_diag_collect(struct hw_diag_summary *out);
void hw_diag_dump(void);

#endif /* MYOS_HW_DIAG_H */
