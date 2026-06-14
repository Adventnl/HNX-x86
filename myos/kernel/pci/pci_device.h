/* A discovered PCI function. */
#ifndef MYOS_PCI_DEVICE_H
#define MYOS_PCI_DEVICE_H

#include "types.h"

struct pci_device {
    uint8_t  bus, slot, func;
    uint16_t vendor, device;
    uint8_t  class_code, subclass, prog_if, revision;
    uint8_t  header_type;
    uint8_t  irq_line, irq_pin;
    uint32_t bar[6];
    uint8_t  in_use;
};

/* Resolve BAR `idx` to a base address. *is_mmio set to 1 for memory BARs, 0 for
 * I/O BARs. Handles 64-bit memory BARs (consuming idx+1). Returns 0 if absent. */
uint64_t pci_device_bar(const struct pci_device *dev, int idx, int *is_mmio);

/* Enable bus mastering + memory space in the command register. */
void pci_device_enable(const struct pci_device *dev);

#endif /* MYOS_PCI_DEVICE_H */
