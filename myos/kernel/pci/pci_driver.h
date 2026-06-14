/* PCI driver registry + class-based matching. Storage drivers (AHCI, NVMe)
 * register here and are bound by pci_driver_match_all(). */
#ifndef MYOS_PCI_DRIVER_H
#define MYOS_PCI_DRIVER_H

#include "types.h"

struct pci_device;

struct pci_driver {
    const char *name;
    uint8_t     class_code;
    uint8_t     subclass;
    uint8_t     prog_if;
    uint8_t     match_prog_if;     /* 1 = prog_if must also match */
    int (*probe)(const struct pci_device *dev);
    struct pci_driver *next;
};

void pci_driver_register(struct pci_driver *drv);

/* Probe every PCI function against registered drivers. Returns bind count. */
int pci_driver_match_all(void);

#endif /* MYOS_PCI_DRIVER_H */
