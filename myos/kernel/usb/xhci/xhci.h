/* xHCI host controller driver — public surface + shared controller state.
 *
 * MyOS drives the controller in a polled model: commands and transfers are
 * posted to rings and the event ring is polled for completion. This keeps
 * enumeration deterministic within the verification window and avoids wiring an
 * MSI handler before the interrupt path is needed. */
#ifndef MYOS_XHCI_H
#define MYOS_XHCI_H

#include "types.h"
#include "xhci_regs.h"

struct pci_device;
struct usb_device;

#define XHCI_MAX_SLOTS_CAP 64

struct xhci {
    struct pci_device *pci;

    volatile uint8_t  *mmio;        /* BAR0 base                      */
    volatile uint8_t  *op;          /* operational registers          */
    volatile uint8_t  *rt;          /* runtime registers              */
    volatile uint32_t *db;          /* doorbell array                 */

    uint8_t   caplen;
    uint16_t  version;
    uint8_t   max_slots;
    uint8_t   max_ports;
    uint16_t  max_intrs;
    uint8_t   context_size;         /* 32 or 64 bytes                 */
    uint8_t   ac64;                 /* 64-bit addressing capable      */
    uint32_t  page_size;
    uint32_t  max_scratchpad;

    uint64_t        *dcbaa;         /* device context base array      */
    struct xhci_trb *cmd_ring;      /* command ring                   */
    uint32_t         cmd_enqueue;   /* next command slot              */
    uint8_t          cmd_cycle;     /* producer cycle state           */

    struct xhci_trb        *event_ring;
    struct xhci_erst_entry *erst;
    uint32_t                event_dequeue;
    uint8_t                 event_cycle;   /* consumer cycle state     */

    uint64_t  scratchpad_array;     /* phys of scratchpad pointer array */
    uint64_t  bounce_phys;          /* identity-mapped DMA bounce page   */

    /* Per-slot device context + EP0 transfer ring (filled during enumeration). */
    void            *dev_context[XHCI_MAX_SLOTS_CAP];
    struct xhci_trb *ep0_ring[XHCI_MAX_SLOTS_CAP];
    uint8_t          ep0_cycle[XHCI_MAX_SLOTS_CAP];
    uint32_t         ep0_enqueue[XHCI_MAX_SLOTS_CAP];
    /* Interrupt-IN endpoint ring per slot (HID), DCI cached by the HID driver. */
    struct xhci_trb *intr_ring[XHCI_MAX_SLOTS_CAP];
    uint8_t          intr_cycle[XHCI_MAX_SLOTS_CAP];
    uint32_t         intr_enqueue[XHCI_MAX_SLOTS_CAP];
    uint8_t          intr_dci[XHCI_MAX_SLOTS_CAP];

    int       initialized;
    int       ports_connected;
};

/* ---- shared MMIO accessors ------------------------------------------------ */
static inline uint32_t xhci_read32(volatile uint8_t *base, uint32_t off) {
    return *(volatile uint32_t *)(base + off);
}
static inline void xhci_write32(volatile uint8_t *base, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(base + off) = v;
}
static inline uint64_t xhci_read64(volatile uint8_t *base, uint32_t off) {
    return (uint64_t)xhci_read32(base, off) |
           ((uint64_t)xhci_read32(base, off + 4) << 32);
}
static inline void xhci_write64(volatile uint8_t *base, uint32_t off, uint64_t v) {
    xhci_write32(base, off, (uint32_t)(v & 0xFFFFFFFF));
    xhci_write32(base, off + 4, (uint32_t)(v >> 32));
}

/* ---- driver entry points -------------------------------------------------- */
/* Discover + bring up the first xHCI controller. Emits the xHCI markers. */
void         xhci_init(void);
struct xhci *xhci_controller(void);   /* the (single) controller, or NULL */

/* Root hub scan: enumerate connected ports, drive USB enumeration per device. */
void xhci_scan_root_hub(struct xhci *xhc);

/* Register this controller as a USB bus and enumerate its connected devices
 * into the USB core (called after usb_core_init / usb_hub_init). */
void xhci_attach_usb(void);

#endif /* MYOS_XHCI_H */
