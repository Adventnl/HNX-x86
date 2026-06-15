/* Hardware diagnostics snapshot (see hw_diag.h). */
#include "hw_diag.h"
#include "pci.h"
#include "driver_registry.h"
#include "block_registry.h"
#include "hw_event_bus.h"
#include "irq.h"
#include "log.h"

/* Resolved once the USB core links in (Prompt 6 Phase D); 0 until then. */
int usb_device_count(void) __attribute__((weak));

void hw_diag_collect(struct hw_diag_summary *out) {
    if (!out) {
        return;
    }
    out->pci_functions = (uint32_t)pci_device_count();
    out->devices = (uint32_t)device_count();
    out->block_devices = (uint32_t)block_device_count();
    out->usb_devices = usb_device_count ? (uint32_t)usb_device_count() : 0;

    uint64_t total = 0;
    uint32_t active = 0;
    for (uint16_t v = 0x20; v <= 0x4F; v++) {
        uint64_t c = irq_count_for_vector((uint8_t)v);
        if (c) {
            active++;
            total += c;
        }
    }
    out->irq_active_vectors = active;
    out->irq_total = total;
    out->hw_events = hw_event_count();
}

void hw_diag_dump(void) {
    struct hw_diag_summary s;
    hw_diag_collect(&s);
    kernel_log_line("    hardware diagnostics:");
    kernel_log_hex64("      pci functions : ", s.pci_functions);
    kernel_log_hex64("      devices       : ", s.devices);
    kernel_log_hex64("      block devices : ", s.block_devices);
    kernel_log_hex64("      usb devices   : ", s.usb_devices);
    kernel_log_hex64("      irq vectors   : ", s.irq_active_vectors);
    kernel_log_hex64("      irq total     : ", s.irq_total);
    kernel_log_hex64("      hw events     : ", s.hw_events);
}
