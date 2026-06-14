/* NVMe foundation: PCI discovery + controller register inspection.
 * Block I/O is deferred (Prompt 5 scope) — the controller is identified and its
 * capabilities logged, but no namespace block device is registered. */
#ifndef MYOS_NVME_H
#define MYOS_NVME_H

#include "types.h"

/* NVMe controller MMIO register offsets. */
#define NVME_REG_CAP   0x00    /* 64-bit capabilities */
#define NVME_REG_VS    0x08    /* version */
#define NVME_REG_CC    0x14    /* controller config */
#define NVME_REG_CSTS  0x1C    /* controller status */
#define NVME_REG_AQA   0x24
#define NVME_REG_ASQ   0x28
#define NVME_REG_ACQ   0x30

void nvme_init(void);   /* register the PCI driver */

#endif /* MYOS_NVME_H */
