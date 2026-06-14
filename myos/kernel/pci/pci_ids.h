/* Human-readable names for PCI class/subclass and a few known vendors. */
#ifndef MYOS_PCI_IDS_H
#define MYOS_PCI_IDS_H

#include "types.h"

const char *pci_class_name(uint8_t class_code, uint8_t subclass);
const char *pci_vendor_name(uint16_t vendor);

#endif /* MYOS_PCI_IDS_H */
