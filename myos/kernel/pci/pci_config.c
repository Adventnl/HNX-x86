/* CF8/CFC PCI configuration mechanism #1. */
#include "pci_config.h"
#include "cpu.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static uint32_t make_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return 0x80000000u |
           ((uint32_t)bus << 16) |
           ((uint32_t)(slot & 0x1F) << 11) |
           ((uint32_t)(func & 0x07) << 8) |
           ((uint32_t)offset & 0xFC);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    x86_outl(PCI_CONFIG_ADDRESS, make_address(bus, slot, func, offset));
    return x86_inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t v = pci_config_read32(bus, slot, func, offset & 0xFC);
    return (uint16_t)((v >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t v = pci_config_read32(bus, slot, func, offset & 0xFC);
    return (uint8_t)((v >> ((offset & 3) * 8)) & 0xFF);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    x86_outl(PCI_CONFIG_ADDRESS, make_address(bus, slot, func, offset));
    x86_outl(PCI_CONFIG_DATA, value);
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t addr = make_address(bus, slot, func, offset);
    x86_outl(PCI_CONFIG_ADDRESS, addr);
    uint32_t cur = x86_inl(PCI_CONFIG_DATA);
    uint32_t shift = (offset & 2) * 8;
    cur &= ~(0xFFFFu << shift);
    cur |= (uint32_t)value << shift;
    x86_outl(PCI_CONFIG_ADDRESS, addr);
    x86_outl(PCI_CONFIG_DATA, cur);
}
