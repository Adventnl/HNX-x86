/* MSI-X table parsing + programming (see msix.h). */
#include "msix.h"
#include "pci_caps.h"
#include "pci_config.h"
#include "pci_device.h"
#include "vmm.h"
#include "memory_layout.h"
#include "apic.h"
#include "log.h"

#define MSIX_CTRL          0x02     /* 16-bit message control               */
#define MSIX_TABLE         0x04     /* 32-bit table offset/BIR              */
#define MSIX_CTRL_ENABLE   0x8000   /* bit 15                               */
#define MSIX_CTRL_FUNCMASK 0x4000   /* bit 14                               */
#define MSIX_CTRL_SIZE     0x07FF   /* bits 0-10 = table size minus one     */

#define MSIX_ENTRY_BYTES   16
#define MSIX_ENTRY_ADDR_LO 0x00
#define MSIX_ENTRY_ADDR_HI 0x04
#define MSIX_ENTRY_DATA    0x08
#define MSIX_ENTRY_VCTRL   0x0C
#define MSIX_VCTRL_MASK    0x00000001u

#define MSI_ADDR_BASE      0xFEE00000u

/* Small cache of mapped tables (device tree is tiny). */
struct msix_map { uint8_t bus, slot, func, valid; volatile uint8_t *table; uint16_t count; };
static struct msix_map g_maps[8];

static struct msix_map *lookup(struct pci_device *dev) {
    for (unsigned i = 0; i < sizeof(g_maps) / sizeof(g_maps[0]); i++) {
        if (g_maps[i].valid && g_maps[i].bus == dev->bus &&
            g_maps[i].slot == dev->slot && g_maps[i].func == dev->func) {
            return &g_maps[i];
        }
    }
    return 0;
}

static struct msix_map *alloc_slot(struct pci_device *dev) {
    struct msix_map *m = lookup(dev);
    if (m) {
        return m;
    }
    for (unsigned i = 0; i < sizeof(g_maps) / sizeof(g_maps[0]); i++) {
        if (!g_maps[i].valid) {
            g_maps[i].bus = dev->bus; g_maps[i].slot = dev->slot; g_maps[i].func = dev->func;
            return &g_maps[i];
        }
    }
    return 0;
}

int msix_supported(struct pci_device *dev) {
    return pci_find_capability(dev, PCI_CAP_ID_MSIX) >= 0 ? 1 : 0;
}

int msix_table_size(struct pci_device *dev) {
    int cap = pci_find_capability(dev, PCI_CAP_ID_MSIX);
    if (cap < 0) {
        return -1;
    }
    uint16_t ctrl = pci_config_read16(dev->bus, dev->slot, dev->func, (uint8_t)(cap + MSIX_CTRL));
    return (int)(ctrl & MSIX_CTRL_SIZE) + 1;
}

int msix_map_table(struct pci_device *dev) {
    int cap = pci_find_capability(dev, PCI_CAP_ID_MSIX);
    if (cap < 0) {
        return -1;
    }
    uint32_t tbl = pci_config_read32(dev->bus, dev->slot, dev->func, (uint8_t)(cap + MSIX_TABLE));
    uint8_t  bir = tbl & 0x7;
    uint32_t off = tbl & ~0x7u;

    int is_mmio = 0;
    uint64_t bar = pci_device_bar(dev, bir, &is_mmio);
    if (!bar || !is_mmio) {
        return -1;
    }
    uint64_t table_phys = bar + off;
    if (vmm_map_mmio_2m(table_phys & ~LARGE_PAGE_MASK) != 0) {
        return -1;
    }
    struct msix_map *m = alloc_slot(dev);
    if (!m) {
        return -1;
    }
    m->table = (volatile uint8_t *)(uintptr_t)table_phys;
    m->count = (uint16_t)msix_table_size(dev);
    m->valid = 1;
    return 0;
}

static void put32(volatile uint8_t *p, uint32_t v) { *(volatile uint32_t *)p = v; }
static uint32_t get32(volatile uint8_t *p) { return *(volatile uint32_t *)p; }

int msix_enable_vector(struct pci_device *dev, uint16_t table_index, uint8_t vector) {
    struct msix_map *m = lookup(dev);
    if (!m || table_index >= m->count) {
        return -1;
    }
    volatile uint8_t *e = m->table + (uint32_t)table_index * MSIX_ENTRY_BYTES;
    uint32_t addr = MSI_ADDR_BASE | (((lapic_read(0x20) >> 24) & 0xFF) << 12);
    put32(e + MSIX_ENTRY_ADDR_LO, addr);
    put32(e + MSIX_ENTRY_ADDR_HI, 0);
    put32(e + MSIX_ENTRY_DATA, vector);
    put32(e + MSIX_ENTRY_VCTRL, get32(e + MSIX_ENTRY_VCTRL) & ~MSIX_VCTRL_MASK);  /* unmask */

    int cap = pci_find_capability(dev, PCI_CAP_ID_MSIX);
    uint16_t ctrl = pci_config_read16(dev->bus, dev->slot, dev->func, (uint8_t)(cap + MSIX_CTRL));
    ctrl &= (uint16_t)~MSIX_CTRL_FUNCMASK;
    ctrl |= MSIX_CTRL_ENABLE;
    pci_config_write16(dev->bus, dev->slot, dev->func, (uint8_t)(cap + MSIX_CTRL), ctrl);
    return 0;
}

void msix_disable(struct pci_device *dev) {
    int cap = pci_find_capability(dev, PCI_CAP_ID_MSIX);
    if (cap < 0) {
        return;
    }
    uint16_t ctrl = pci_config_read16(dev->bus, dev->slot, dev->func, (uint8_t)(cap + MSIX_CTRL));
    ctrl &= (uint16_t)~MSIX_CTRL_ENABLE;
    pci_config_write16(dev->bus, dev->slot, dev->func, (uint8_t)(cap + MSIX_CTRL), ctrl);
}
