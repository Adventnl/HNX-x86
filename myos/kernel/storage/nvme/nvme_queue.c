/* NVMe queue ring allocation (admin queue foundation). */
#include "nvme_queue.h"
#include "pmm.h"
#include "memory_layout.h"
#include "string.h"

static struct nvme_queue g_admin;

int nvme_queue_foundation(void) {
    uint64_t sq = pmm_alloc_page();
    uint64_t cq = pmm_alloc_page();
    if (sq == PMM_INVALID_PAGE || cq == PMM_INVALID_PAGE) {
        return -1;
    }
    memset((void *)(uintptr_t)sq, 0, PAGE_SIZE);
    memset((void *)(uintptr_t)cq, 0, PAGE_SIZE);
    g_admin.sq_phys = sq;
    g_admin.cq_phys = cq;
    g_admin.entries = PAGE_SIZE / 64;   /* 64-byte SQ entries */
    g_admin.sq_tail = 0;
    g_admin.cq_head = 0;
    g_admin.phase = 1;
    return 0;
}
