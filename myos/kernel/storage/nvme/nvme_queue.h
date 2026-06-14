/* NVMe submission/completion queue model (foundation). */
#ifndef MYOS_NVME_QUEUE_H
#define MYOS_NVME_QUEUE_H

#include "types.h"

struct nvme_queue {
    uint64_t sq_phys;       /* submission queue ring */
    uint64_t cq_phys;       /* completion queue ring */
    uint32_t entries;
    uint32_t sq_tail;
    uint32_t cq_head;
    uint8_t  phase;
};

/* Allocate the admin queue rings (foundation; not yet submitting commands). */
int nvme_queue_foundation(void);

#endif /* MYOS_NVME_QUEUE_H */
