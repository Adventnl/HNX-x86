/* xHCI controller bring-up (see xhci_controller.h). */
#include "xhci_controller.h"
#include "xhci_ring.h"
#include "xhci_context.h"
#include "pci_device.h"
#include "vmm.h"
#include "memory_layout.h"
#include "pmm.h"
#include "string.h"
#include "log.h"

#define XHCI_SPIN 5000000u

static int wait_clear(volatile uint8_t *base, uint32_t off, uint32_t mask) {
    for (uint32_t spin = 0; spin < XHCI_SPIN; spin++) {
        if (!(xhci_read32(base, off) & mask)) {
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

static int map_and_parse(struct xhci *xhc) {
    int is_mmio = 0;
    uint64_t bar = pci_device_bar(xhc->pci, 0, &is_mmio);
    if (!bar || !is_mmio) {
        kernel_log_error("xHCI: BAR0 is not MMIO");
        return -1;
    }
    pci_device_enable(xhc->pci);
    if (vmm_map_mmio_2m(bar & ~LARGE_PAGE_MASK) != 0) {
        kernel_log_error("xHCI: MMIO map failed");
        return -1;
    }
    xhc->mmio = (volatile uint8_t *)(uintptr_t)bar;
    kernel_log_ok("xHCI MMIO mapped");
    kernel_log_hex64("    xhci bar0      : ", bar);

    /* CAPLENGTH[7:0] + HCIVERSION[31:16] share the first 32-bit register; some
     * controllers only honour 32-bit MMIO reads here. */
    uint32_t cap0 = xhci_read32(xhc->mmio, XHCI_CAP_CAPLENGTH);
    xhc->caplen = (uint8_t)(cap0 & 0xFF);
    xhc->version = (uint16_t)((cap0 >> 16) & 0xFFFF);

    uint32_t hcs1 = xhci_read32(xhc->mmio, XHCI_CAP_HCSPARAMS1);
    xhc->max_slots = (uint8_t)XHCI_HCS1_MAXSLOTS(hcs1);
    xhc->max_intrs = (uint16_t)XHCI_HCS1_MAXINTRS(hcs1);
    xhc->max_ports = (uint8_t)XHCI_HCS1_MAXPORTS(hcs1);
    if (xhc->max_slots > XHCI_MAX_SLOTS_CAP - 1) {
        xhc->max_slots = XHCI_MAX_SLOTS_CAP - 1;
    }

    uint32_t hcs2 = xhci_read32(xhc->mmio, XHCI_CAP_HCSPARAMS2);
    xhc->max_scratchpad = XHCI_HCS2_MAXSCRATCH(hcs2);

    uint32_t hcc1 = xhci_read32(xhc->mmio, XHCI_CAP_HCCPARAMS1);
    xhc->ac64 = (uint8_t)XHCI_HCC1_AC64(hcc1);
    xhc->context_size = XHCI_HCC1_CSZ(hcc1) ? 64 : 32;

    xhc->op = xhc->mmio + xhc->caplen;
    xhc->rt = xhc->mmio + (xhci_read32(xhc->mmio, XHCI_CAP_RTSOFF) & ~0x1Fu);
    xhc->db = (volatile uint32_t *)(xhc->mmio + (xhci_read32(xhc->mmio, XHCI_CAP_DBOFF) & ~0x3u));
    xhc->page_size = xhci_read32(xhc->op, XHCI_OP_PAGESIZE);

    kernel_log_ok("xHCI capability registers parsed");
    kernel_log_hex64("    xhci version   : ", xhc->version);
    kernel_log_hex64("    xhci max slots : ", xhc->max_slots);
    kernel_log_hex64("    xhci max ports : ", xhc->max_ports);
    kernel_log_hex64("    xhci ctx size  : ", xhc->context_size);
    return 0;
}

static int reset_controller(struct xhci *xhc) {
    /* Stop the controller if running, then wait for it to halt (HCH set). */
    uint32_t cmd = xhci_read32(xhc->op, XHCI_OP_USBCMD);
    cmd &= ~XHCI_CMD_RUN;
    xhci_write32(xhc->op, XHCI_OP_USBCMD, cmd);
    for (uint32_t spin = 0; spin < XHCI_SPIN; spin++) {
        if (xhci_read32(xhc->op, XHCI_OP_USBSTS) & XHCI_STS_HCH) {
            break;
        }
        __asm__ volatile("pause");
    }

    /* Host controller reset. */
    cmd = xhci_read32(xhc->op, XHCI_OP_USBCMD) | XHCI_CMD_HCRST;
    xhci_write32(xhc->op, XHCI_OP_USBCMD, cmd);
    if (wait_clear(xhc->op, XHCI_OP_USBCMD, XHCI_CMD_HCRST) != 0) {
        kernel_log_error("xHCI: reset (HCRST) timed out");
        return -1;
    }
    if (wait_clear(xhc->op, XHCI_OP_USBSTS, XHCI_STS_CNR) != 0) {
        kernel_log_error("xHCI: controller-not-ready timed out");
        return -1;
    }
    return 0;
}

static int setup_event_ring(struct xhci *xhc) {
    uint64_t evt_page = pmm_alloc_page();
    uint64_t erst_page = pmm_alloc_page();
    if (evt_page == PMM_INVALID_PAGE || erst_page == PMM_INVALID_PAGE) {
        return -1;
    }
    memset((void *)(uintptr_t)evt_page, 0, PAGE_SIZE);
    memset((void *)(uintptr_t)erst_page, 0, PAGE_SIZE);

    xhc->event_ring = (struct xhci_trb *)(uintptr_t)evt_page;
    xhc->event_dequeue = 0;
    xhc->event_cycle = 1;
    xhc->erst = (struct xhci_erst_entry *)(uintptr_t)erst_page;
    xhc->erst[0].ring_base = evt_page;
    xhc->erst[0].ring_size = XHCI_RING_TRBS;
    xhc->erst[0].reserved = 0;

    /* Interrupter 0: size, dequeue pointer, then base (base write arms it). */
    xhci_write32(xhc->rt, XHCI_RT_IR0 + XHCI_IR_ERSTSZ, 1);
    xhci_write64(xhc->rt, XHCI_RT_IR0 + XHCI_IR_ERDP, evt_page);
    xhci_write64(xhc->rt, XHCI_RT_IR0 + XHCI_IR_ERSTBA, erst_page);
    /* Leave interrupts disabled (IE=0): MyOS polls the event ring. */
    xhci_write32(xhc->rt, XHCI_RT_IR0 + XHCI_IR_IMAN, XHCI_IMAN_IP);
    return 0;
}

int xhci_controller_setup(struct xhci *xhc) {
    if (map_and_parse(xhc) != 0) {
        return -1;
    }
    if (reset_controller(xhc) != 0) {
        return -1;
    }

    /* Enable all device slots. */
    xhci_write32(xhc->op, XHCI_OP_CONFIG, xhc->max_slots);

    /* DCBAA + scratchpad. */
    if (xhci_context_init(xhc) != 0) {
        kernel_log_error("xHCI: context init failed");
        return -1;
    }

    /* DMA bounce page for control-transfer payloads (callers may pass heap
     * pointers, which are not identity-mapped / DMA-addressable). */
    xhc->bounce_phys = pmm_alloc_page();
    if (xhc->bounce_phys == PMM_INVALID_PAGE) {
        return -1;
    }
    memset((void *)(uintptr_t)xhc->bounce_phys, 0, PAGE_SIZE);

    /* Command ring. */
    xhc->cmd_ring = xhci_ring_alloc();
    if (!xhc->cmd_ring) {
        return -1;
    }
    xhc->cmd_enqueue = 0;
    xhc->cmd_cycle = 1;
    xhci_write64(xhc->op, XHCI_OP_CRCR, (uint64_t)(uintptr_t)xhc->cmd_ring | 1 /*RCS*/);
    kernel_log_ok("xHCI command ring online");

    /* Event ring. */
    if (setup_event_ring(xhc) != 0) {
        kernel_log_error("xHCI: event ring alloc failed");
        return -1;
    }
    kernel_log_ok("xHCI event ring online");

    /* Run. */
    uint32_t cmd = xhci_read32(xhc->op, XHCI_OP_USBCMD) | XHCI_CMD_RUN;
    xhci_write32(xhc->op, XHCI_OP_USBCMD, cmd);
    if (wait_clear(xhc->op, XHCI_OP_USBSTS, XHCI_STS_HCH) != 0) {
        kernel_log_error("xHCI: controller did not start");
        return -1;
    }
    xhc->initialized = 1;
    kernel_log_ok("xHCI controller started");
    return 0;
}
