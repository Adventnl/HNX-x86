/* PCI bus driver: enumerate config space, expose discovered functions. */
#ifndef MYOS_PCI_H
#define MYOS_PCI_H

#include "types.h"
#include "pci_device.h"

#define PCI_MAX_DEVICES 64

void pci_init(void);
void pci_scan_all(void);

const struct pci_device *pci_find_by_class(uint8_t class_code, uint8_t subclass);
const struct pci_device *pci_find_by_class_prog(uint8_t class_code, uint8_t subclass, uint8_t prog_if);
const struct pci_device *pci_find_by_id(uint16_t vendor, uint16_t device);

int                      pci_device_count(void);
const struct pci_device *pci_device_at(int index);

void pci_dump_devices(void);

/* Register every discovered function into the driver-core device registry
 * (DEV_TYPE_PCI) so userspace `lspci`/`devices` can enumerate them. */
void pci_register_devices(void);

#endif /* MYOS_PCI_H */
