/* NVMe controller register inspection. */
#include "nvme_controller.h"
#include "nvme.h"
#include "nvme_queue.h"
#include "nvme_namespace.h"
#include "vmm.h"
#include "memory_layout.h"
#include "log.h"

static uint32_t reg32(volatile uint8_t *base, uint32_t off) {
    return *(volatile uint32_t *)(base + off);
}

int nvme_controller_init(uint64_t bar0, struct nvme_controller *out) {
    if (vmm_map_mmio_2m(bar0 & ~LARGE_PAGE_MASK) != 0) {
        return -1;
    }
    out->regs = (volatile uint8_t *)(uintptr_t)bar0;

    uint32_t cap_lo = reg32(out->regs, NVME_REG_CAP);
    uint32_t cap_hi = reg32(out->regs, NVME_REG_CAP + 4);
    out->cap = ((uint64_t)cap_hi << 32) | cap_lo;
    out->version = reg32(out->regs, NVME_REG_VS);
    uint32_t csts = reg32(out->regs, NVME_REG_CSTS);

    out->max_queue_entries = (cap_lo & 0xFFFF) + 1;
    out->doorbell_stride = (cap_hi >> 0) & 0xF;   /* CAP.DSTRD (bits 35:32) */

    kernel_log_ok("NVMe controller found");
    kernel_log_hex64("    nvme cap       : ", out->cap);
    kernel_log_hex64("    nvme version   : ", out->version);
    kernel_log_hex64("    nvme csts      : ", csts);
    kernel_log_hex64("    nvme max qe    : ", out->max_queue_entries);

    /* Foundation: allocate the admin queue memory (not yet driven). */
    nvme_queue_foundation();

    /* Namespace + block I/O are deferred in Prompt 5. */
    nvme_namespace_probe(out);
    return 0;
}
