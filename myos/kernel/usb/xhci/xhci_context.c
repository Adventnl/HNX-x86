/* xHCI context / DCBAA / scratchpad allocation (see xhci_context.h). */
#include "xhci_context.h"
#include "pmm.h"
#include "memory_layout.h"
#include "string.h"
#include "log.h"

int xhci_context_init(struct xhci *xhc) {
    /* Device Context Base Address Array: (max_slots + 1) 64-bit entries. */
    uint64_t dcbaa_page = pmm_alloc_page();
    if (dcbaa_page == PMM_INVALID_PAGE) {
        return -1;
    }
    memset((void *)(uintptr_t)dcbaa_page, 0, PAGE_SIZE);
    xhc->dcbaa = (uint64_t *)(uintptr_t)dcbaa_page;

    /* Scratchpad buffers, if the controller requests any. */
    if (xhc->max_scratchpad > 0) {
        uint64_t array_page = pmm_alloc_page();
        if (array_page == PMM_INVALID_PAGE) {
            return -1;
        }
        memset((void *)(uintptr_t)array_page, 0, PAGE_SIZE);
        uint64_t *array = (uint64_t *)(uintptr_t)array_page;
        for (uint32_t i = 0; i < xhc->max_scratchpad; i++) {
            uint64_t buf = pmm_alloc_page();
            if (buf == PMM_INVALID_PAGE) {
                return -1;
            }
            memset((void *)(uintptr_t)buf, 0, PAGE_SIZE);
            array[i] = buf;
        }
        xhc->scratchpad_array = array_page;
        xhc->dcbaa[0] = array_page;          /* DCBAA[0] -> scratchpad array */
        kernel_log_hex64("    xhci scratchpad : ", (uint64_t)xhc->max_scratchpad);
    }

    /* Publish the DCBAA to the controller. */
    xhci_write64(xhc->op, XHCI_OP_DCBAAP, dcbaa_page);
    return 0;
}

void *xhci_alloc_context_block(struct xhci *xhc) {
    (void)xhc;
    uint64_t page = pmm_alloc_page();
    if (page == PMM_INVALID_PAGE) {
        return NULL;
    }
    memset((void *)(uintptr_t)page, 0, PAGE_SIZE);
    return (void *)(uintptr_t)page;
}
