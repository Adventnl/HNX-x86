/* PCI capability list parsing. The legacy CF8/CFC config window only reaches the
 * first 256 bytes, which is where every standard capability (MSI 0x05, MSI-X
 * 0x11, PCIe 0x10, Power-Management 0x01) lives. PCIe *extended* capabilities
 * (offset >= 0x100) require an ECAM/MMCONFIG window and are reported honestly as
 * unavailable until that is wired. */
#ifndef MYOS_PCI_CAPS_H
#define MYOS_PCI_CAPS_H

#include "types.h"

struct pci_device;

/* Standard PCI capability IDs. */
#define PCI_CAP_ID_PM    0x01     /* Power Management            */
#define PCI_CAP_ID_MSI   0x05     /* Message Signalled Interrupts*/
#define PCI_CAP_ID_VNDR  0x09     /* Vendor-specific             */
#define PCI_CAP_ID_PCIE  0x10     /* PCI Express                 */
#define PCI_CAP_ID_MSIX  0x11     /* MSI-X                       */

/* Status register bit 4: capability list present. */
#define PCI_STATUS_REG       0x06
#define PCI_STATUS_CAP_LIST  0x0010
#define PCI_CAP_PTR_REG      0x34

/* Return the config-space offset of capability `capability_id`, or -1. */
int  pci_find_capability(struct pci_device *dev, uint8_t capability_id);

/* Return the extended-config offset of a PCIe extended capability, or -1.
 * Honest: requires ECAM, which MyOS does not map yet -> always -1. */
int  pci_find_extended_capability(struct pci_device *dev, uint16_t capability_id);

/* Log every standard capability discovered on `dev`. */
void pci_dump_capabilities(struct pci_device *dev);

#endif /* MYOS_PCI_CAPS_H */
