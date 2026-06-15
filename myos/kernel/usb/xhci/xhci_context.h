/* xHCI device/input context + DCBAA + scratchpad allocation.
 *
 * A context block is one DMA page. Within it, 32/64-byte contexts are addressed
 * by index: for a device context, index 0 is the Slot Context and index N is the
 * endpoint context for DCI N. An input context prefixes an Input Control Context
 * at index 0, shifting the slot/endpoint contexts up by one. */
#ifndef MYOS_XHCI_CONTEXT_H
#define MYOS_XHCI_CONTEXT_H

#include "types.h"
#include "xhci.h"

/* Allocate the DCBAA and (if required) the scratchpad buffer array. Programs
 * DCBAAP. Returns 0 on success. */
int xhci_context_init(struct xhci *xhc);

/* Allocate one zeroed context-block page (device or input context). */
void *xhci_alloc_context_block(struct xhci *xhc);

/* Pointer to the 32/64-byte context at `index` inside a context block. */
static inline uint32_t *xhci_ctx_dword(struct xhci *xhc, void *block, int index) {
    return (uint32_t *)((uint8_t *)block + (uint32_t)index * xhc->context_size);
}

#endif /* MYOS_XHCI_CONTEXT_H */
