/* Message Signalled Interrupts (MSI) foundation. On x86 an MSI is a posted write
 * to 0xFEE00000 | (dest_apic << 12) carrying a data payload of the target IDT
 * vector. This module parses the MSI capability and programs that pair. */
#ifndef MYOS_MSI_H
#define MYOS_MSI_H

#include "types.h"

struct pci_device;

/* Bring the MSI/MSI-X foundation online: parse capabilities on every PCI
 * function and emit the subsystem markers. */
void msi_init(void);

int  msi_supported(struct pci_device *dev);
int  msi_enable(struct pci_device *dev, uint8_t vector);
void msi_disable(struct pci_device *dev);

/* Read the current MSI-enable bit (1/0), or -1 if unsupported. */
int  msi_is_enabled(struct pci_device *dev);

#endif /* MYOS_MSI_H */
