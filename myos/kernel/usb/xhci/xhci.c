/* xHCI top-level: discover the PCI controller, bring it up and scan the root
 * hub. The single controller instance is shared with the USB core, which drives
 * descriptor enumeration over the EP0 rings this layer establishes. */
#include "xhci.h"
#include "xhci_controller.h"
#include "xhci_command.h"
#include "xhci_roothub.h"
#include "xhci_port.h"
#include "xhci_ring.h"
#include "xhci_context.h"
#include "xhci_transfer.h"
#include "xhci_event.h"
#include "pci.h"
#include "pci_device.h"
#include "usb.h"
#include "usb_hub.h"
#include "string.h"
#include "log.h"

/* xHCI: PCI class 0x0C (serial bus), subclass 0x03 (USB), prog-if 0x30. */
#define XHCI_PCI_CLASS    0x0C
#define XHCI_PCI_SUBCLASS 0x03
#define XHCI_PCI_PROGIF   0x30

static struct xhci g_xhci;
static int g_present;

struct xhci *xhci_controller(void) {
    return g_present ? &g_xhci : NULL;
}

void xhci_init(void) {
    const struct pci_device *dev =
        pci_find_by_class_prog(XHCI_PCI_CLASS, XHCI_PCI_SUBCLASS, XHCI_PCI_PROGIF);
    if (!dev) {
        kernel_log_warn("xHCI controller not found");
        return;
    }

    memset(&g_xhci, 0, sizeof(g_xhci));
    g_xhci.pci = (struct pci_device *)dev;
    kernel_log_ok("xHCI controller found");
    kernel_log_hex64("    xhci pci vendor: ", dev->vendor);
    kernel_log_hex64("    xhci pci device: ", dev->device);

    if (xhci_controller_setup(&g_xhci) != 0) {
        kernel_log_error("xHCI: controller setup failed");
        return;
    }
    g_present = 1;

    /* Validate the command + event ring + doorbell path with a No-Op command. */
    if (xhci_cmd_noop(&g_xhci) == XHCI_CC_SUCCESS) {
        kernel_log_ok("xHCI command ring verified (noop)");
    } else {
        kernel_log_warn("xHCI noop command did not complete");
    }

    xhci_scan_root_hub(&g_xhci);
}

/* ---- USB core bus glue ---------------------------------------------------- */

static int xhci_bus_control(struct usb_device *dev, uint8_t bmRequestType, uint8_t bRequest,
                            uint16_t wValue, uint16_t wIndex, uint16_t wLength, void *data) {
    struct xhci *xhc = (struct xhci *)dev->bus->hc;
    return xhci_control_transfer(xhc, dev->hc_slot, bmRequestType, bRequest,
                                 wValue, wIndex, wLength, data) == 0 ? 0 : -1;
}

/* xHCI Interval encoding: 2^Interval 125us microframes. */
static uint8_t encode_interval(uint8_t speed, uint8_t bInterval) {
    if (speed == XHCI_SPEED_HIGH || speed == XHCI_SPEED_SUPER) {
        /* bInterval is already an exponent (1..16). */
        return (uint8_t)(bInterval ? bInterval - 1 : 3);
    }
    /* Full/low speed: bInterval is in frames (ms). Convert to a microframe
     * exponent: floor(log2(bInterval * 8)). */
    uint32_t uframes = (bInterval ? bInterval : 1) * 8;
    uint8_t i = 3;
    while ((1u << (i + 1)) <= uframes && i < 15) {
        i++;
    }
    return i;
}

static int xhci_bus_configure_endpoint(struct usb_device *dev, uint8_t ep_addr,
                                       uint16_t max_packet, uint8_t interval) {
    struct xhci *xhc = (struct xhci *)dev->bus->hc;
    uint8_t slot = dev->hc_slot;
    uint8_t n = ep_addr & 0x0F;
    uint8_t in = (ep_addr & 0x80) ? 1 : 0;
    uint8_t dci = (uint8_t)(2 * n + in);

    struct xhci_trb *ring = xhci_ring_alloc();
    void *inctx = xhci_alloc_context_block(xhc);
    if (!ring || !inctx) {
        return -1;
    }
    xhc->intr_ring[slot] = ring;
    xhc->intr_cycle[slot] = 1;
    xhc->intr_enqueue[slot] = 0;
    xhc->intr_dci[slot] = dci;

    uint32_t *icc   = xhci_ctx_dword(xhc, inctx, 0);
    uint32_t *slotc = xhci_ctx_dword(xhc, inctx, 1);
    uint32_t *epc   = xhci_ctx_dword(xhc, inctx, 1 + dci);

    icc[0] = 0;
    icc[1] = (1u << 0) | (1u << dci);                 /* add slot + this EP */

    slotc[0] = ((uint32_t)dev->speed << 20) | ((uint32_t)dci << 27);
    slotc[1] = ((uint32_t)dev->port << 16);

    uint64_t ring_phys = (uint64_t)(uintptr_t)ring;
    epc[0] = ((uint32_t)encode_interval(dev->speed, interval) << 16);
    epc[1] = (3u << 1) | (7u << 3) | ((uint32_t)max_packet << 16); /* CErr=3, Interrupt-IN */
    epc[2] = (uint32_t)(ring_phys | 1);
    epc[3] = (uint32_t)(ring_phys >> 32);
    epc[4] = (uint32_t)max_packet | ((uint32_t)max_packet << 16);

    return xhci_cmd_configure_endpoint(xhc, slot, (uint64_t)(uintptr_t)inctx)
               == XHCI_CC_SUCCESS ? 0 : -1;
}

static int xhci_bus_submit_interrupt(struct usb_device *dev, uint64_t dma_buf, uint16_t length) {
    struct xhci *xhc = (struct xhci *)dev->bus->hc;
    return xhci_interrupt_queue(xhc, dev->hc_slot, dma_buf, length);
}

static int xhci_bus_poll_interrupt(struct usb_device *dev, int *bytes) {
    struct xhci *xhc = (struct xhci *)dev->bus->hc;
    struct xhci_trb ev;
    if (!xhci_event_try(xhc, &ev)) {
        return 0;
    }
    if (XHCI_TRB_GET_TYPE(ev.control) != XHCI_TRB_TRANSFER_EVENT) {
        return 0;
    }
    if (bytes) {
        /* status[23:0] is the residual length not transferred. */
        *bytes = (int)((uint32_t)dev->config.interface.endpoints[0].max_packet -
                       (ev.status & 0x00FFFFFF));
    }
    return 1;
}

static struct usb_bus g_usb_bus;

void xhci_attach_usb(void) {
    struct xhci *xhc = xhci_controller();
    if (!xhc) {
        return;
    }
    g_usb_bus.name = "xhci0";
    g_usb_bus.hc = xhc;
    g_usb_bus.control = xhci_bus_control;
    g_usb_bus.configure_endpoint = xhci_bus_configure_endpoint;
    g_usb_bus.submit_interrupt = xhci_bus_submit_interrupt;
    g_usb_bus.poll_interrupt = xhci_bus_poll_interrupt;
    usb_register_bus(&g_usb_bus);

    struct usb_hub *hub = usb_hub_register_root("xhci-root", xhc->max_ports);

    int enumerated = 0;
    for (uint8_t p = 1; p <= xhc->max_ports; p++) {
        if (!xhci_port_connected(xhc, p)) {
            continue;
        }
        uint8_t speed = 0;
        int slot = xhci_address_port_device(xhc, p, &speed);
        if (slot <= 0) {
            continue;
        }
        if (hub) {
            usb_hub_set_port(hub, p, 1, speed);
        }
        struct usb_device *dev = usb_alloc_device();
        if (!dev) {
            break;
        }
        memset(dev, 0, sizeof(*dev));
        dev->bus = &g_usb_bus;
        dev->hc_slot = (uint8_t)slot;
        dev->port = p;
        dev->speed = speed;
        if (usb_enumerate_device(dev) != 0) {
            continue;
        }
        usb_set_address(dev, (uint8_t)slot);
        usb_register_device(dev);
        enumerated++;
    }
    kernel_log_hex64("    usb enumerated : ", (uint64_t)enumerated);
}
