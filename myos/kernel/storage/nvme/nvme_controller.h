/* NVMe controller bring-up (register inspection only in Prompt 5). */
#ifndef MYOS_NVME_CONTROLLER_H
#define MYOS_NVME_CONTROLLER_H

#include "types.h"

struct nvme_controller {
    volatile uint8_t *regs;     /* BAR0 MMIO */
    uint64_t cap;
    uint32_t version;
    uint32_t doorbell_stride;
    uint32_t max_queue_entries;
};

int nvme_controller_init(uint64_t bar0, struct nvme_controller *out);

#endif /* MYOS_NVME_CONTROLLER_H */
