/* NVMe namespace probe (deferred). */
#include "nvme_namespace.h"
#include "nvme_controller.h"
#include "nvme_block.h"
#include "log.h"

void nvme_namespace_probe(struct nvme_controller *ctrl) {
    (void)ctrl;
    /* Identify Namespace + I/O queue creation are Prompt 6 work. */
    nvme_block_register_deferred();
}
