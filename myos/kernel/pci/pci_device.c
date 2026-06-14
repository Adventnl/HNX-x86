/* PCI BAR decoding + command-register enable. */
#include "pci_device.h"
#include "pci_config.h"

#define PCI_REG_COMMAND 0x04
#define PCI_CMD_IO      (1u << 0)
#define PCI_CMD_MEM     (1u << 1)
#define PCI_CMD_MASTER  (1u << 2)

uint64_t pci_device_bar(const struct pci_device *dev, int idx, int *is_mmio) {
    if (idx < 0 || idx > 5) {
        if (is_mmio) *is_mmio = 0;
        return 0;
    }
    uint32_t bar = dev->bar[idx];
    if (bar & 1u) {
        /* I/O space BAR. */
        if (is_mmio) *is_mmio = 0;
        return bar & ~0x3u;
    }
    if (is_mmio) *is_mmio = 1;
    uint32_t type = (bar >> 1) & 0x3;
    uint64_t base = bar & ~0xFu;
    if (type == 0x2 && idx < 5) {
        base |= (uint64_t)dev->bar[idx + 1] << 32;   /* 64-bit BAR */
    }
    return base;
}

void pci_device_enable(const struct pci_device *dev) {
    uint16_t cmd = pci_config_read16(dev->bus, dev->slot, dev->func, PCI_REG_COMMAND);
    cmd |= PCI_CMD_IO | PCI_CMD_MEM | PCI_CMD_MASTER;
    pci_config_write16(dev->bus, dev->slot, dev->func, PCI_REG_COMMAND, cmd);
}
