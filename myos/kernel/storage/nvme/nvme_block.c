/* NVMe block device — deferred. */
#include "nvme_block.h"
#include "log.h"

void nvme_block_register_deferred(void) {
    kernel_log_warn("NVMe block I/O deferred");
}
