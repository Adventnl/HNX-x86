/* PCI configuration space access via the legacy CF8/CFC I/O ports. */
#ifndef MYOS_PCI_CONFIG_H
#define MYOS_PCI_CONFIG_H

#include "types.h"

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t  pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void     pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);

#endif /* MYOS_PCI_CONFIG_H */
