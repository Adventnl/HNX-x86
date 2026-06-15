/* PCI capability list walking (see pci_caps.h). */
#include "pci_caps.h"
#include "pci_config.h"
#include "pci_device.h"
#include "log.h"

static const char *cap_name(uint8_t id) {
    switch (id) {
    case PCI_CAP_ID_PM:   return "power-management";
    case PCI_CAP_ID_MSI:  return "msi";
    case PCI_CAP_ID_VNDR: return "vendor-specific";
    case PCI_CAP_ID_PCIE: return "pci-express";
    case PCI_CAP_ID_MSIX: return "msi-x";
    default:              return "capability";
    }
}

int pci_find_capability(struct pci_device *dev, uint8_t capability_id) {
    uint16_t status = pci_config_read16(dev->bus, dev->slot, dev->func, PCI_STATUS_REG);
    if (!(status & PCI_STATUS_CAP_LIST)) {
        return -1;
    }
    uint8_t ptr = pci_config_read8(dev->bus, dev->slot, dev->func, PCI_CAP_PTR_REG) & 0xFC;
    /* The list is bounded by config space (256/4 entries); guard against loops. */
    for (int guard = 0; ptr != 0 && guard < 48; guard++) {
        uint8_t id = pci_config_read8(dev->bus, dev->slot, dev->func, ptr);
        if (id == 0xFF) {
            break;
        }
        if (id == capability_id) {
            return (int)ptr;
        }
        ptr = pci_config_read8(dev->bus, dev->slot, dev->func, (uint8_t)(ptr + 1)) & 0xFC;
    }
    return -1;
}

int pci_find_extended_capability(struct pci_device *dev, uint16_t capability_id) {
    (void)dev;
    (void)capability_id;
    /* PCIe extended caps live at offset >= 0x100 and need ECAM/MMCONFIG, which
     * MyOS does not map yet. Report honestly rather than fake a hit. */
    return -1;
}

void pci_dump_capabilities(struct pci_device *dev) {
    uint16_t status = pci_config_read16(dev->bus, dev->slot, dev->func, PCI_STATUS_REG);
    if (!(status & PCI_STATUS_CAP_LIST)) {
        kernel_log_line("    (no capability list)");
        return;
    }
    uint8_t ptr = pci_config_read8(dev->bus, dev->slot, dev->func, PCI_CAP_PTR_REG) & 0xFC;
    for (int guard = 0; ptr != 0 && guard < 48; guard++) {
        uint8_t id = pci_config_read8(dev->bus, dev->slot, dev->func, ptr);
        if (id == 0xFF) {
            break;
        }
        kernel_log("    cap ");
        kernel_log(cap_name(id));
        kernel_log_hex64("  @off=", ptr);
        ptr = pci_config_read8(dev->bus, dev->slot, dev->func, (uint8_t)(ptr + 1)) & 0xFC;
    }
}
