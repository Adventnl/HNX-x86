/* MSI capability programming (see msi.h). */
#include "msi.h"
#include "msix.h"
#include "pci_caps.h"
#include "pci_config.h"
#include "pci_device.h"
#include "pci.h"
#include "apic.h"
#include "log.h"

/* MSI capability register offsets, relative to the capability base. */
#define MSI_CTRL          0x02      /* 16-bit message control            */
#define MSI_ADDR_LO       0x04      /* 32-bit message address (low)      */
#define MSI_CTRL_ENABLE   0x0001    /* bit 0: MSI enable                 */
#define MSI_CTRL_64BIT    0x0080    /* bit 7: 64-bit address capable     */
#define MSI_CTRL_MME_MASK 0x0070    /* bits 4-6: multiple message enable */

/* Intel SDM: MSI address targets the local APIC at 0xFEE00000. */
#define MSI_ADDR_BASE     0xFEE00000u

static uint8_t bsp_apic_id(void) {
    /* Local APIC ID register (offset 0x20), id in bits 24-31. */
    return (uint8_t)((lapic_read(0x20) >> 24) & 0xFF);
}

int msi_supported(struct pci_device *dev) {
    return pci_find_capability(dev, PCI_CAP_ID_MSI) >= 0 ? 1 : 0;
}

int msi_is_enabled(struct pci_device *dev) {
    int cap = pci_find_capability(dev, PCI_CAP_ID_MSI);
    if (cap < 0) {
        return -1;
    }
    uint16_t ctrl = pci_config_read16(dev->bus, dev->slot, dev->func, (uint8_t)(cap + MSI_CTRL));
    return (ctrl & MSI_CTRL_ENABLE) ? 1 : 0;
}

int msi_enable(struct pci_device *dev, uint8_t vector) {
    int cap = pci_find_capability(dev, PCI_CAP_ID_MSI);
    if (cap < 0) {
        return -1;
    }
    uint16_t ctrl = pci_config_read16(dev->bus, dev->slot, dev->func, (uint8_t)(cap + MSI_CTRL));
    int is64 = (ctrl & MSI_CTRL_64BIT) != 0;

    uint32_t addr = MSI_ADDR_BASE | ((uint32_t)bsp_apic_id() << 12);
    uint16_t data = vector;            /* fixed delivery, edge-triggered */

    pci_config_write32(dev->bus, dev->slot, dev->func, (uint8_t)(cap + MSI_ADDR_LO), addr);
    if (is64) {
        pci_config_write32(dev->bus, dev->slot, dev->func, (uint8_t)(cap + 0x08), 0);
        pci_config_write16(dev->bus, dev->slot, dev->func, (uint8_t)(cap + 0x0C), data);
    } else {
        pci_config_write16(dev->bus, dev->slot, dev->func, (uint8_t)(cap + 0x08), data);
    }

    ctrl &= (uint16_t)~MSI_CTRL_MME_MASK;   /* one vector */
    ctrl |= MSI_CTRL_ENABLE;
    pci_config_write16(dev->bus, dev->slot, dev->func, (uint8_t)(cap + MSI_CTRL), ctrl);
    return 0;
}

void msi_disable(struct pci_device *dev) {
    int cap = pci_find_capability(dev, PCI_CAP_ID_MSI);
    if (cap < 0) {
        return;
    }
    uint16_t ctrl = pci_config_read16(dev->bus, dev->slot, dev->func, (uint8_t)(cap + MSI_CTRL));
    ctrl &= (uint16_t)~MSI_CTRL_ENABLE;
    pci_config_write16(dev->bus, dev->slot, dev->func, (uint8_t)(cap + MSI_CTRL), ctrl);
}

void msi_init(void) {
    int functions = pci_device_count();
    int with_caps = 0, with_msi = 0, with_msix = 0;
    for (int i = 0; i < functions; i++) {
        struct pci_device *d = (struct pci_device *)pci_device_at(i);
        if (!d) {
            continue;
        }
        uint16_t status = pci_config_read16(d->bus, d->slot, d->func, PCI_STATUS_REG);
        if (status & PCI_STATUS_CAP_LIST) {
            with_caps++;
        }
        if (msi_supported(d)) {
            with_msi++;
        }
        if (msix_supported(d)) {
            with_msix++;
        }
    }
    kernel_log_ok("PCI capabilities parsed");
    kernel_log_hex64("    functions w/ caps : ", (uint64_t)with_caps);
    kernel_log_ok("MSI foundation online");
    kernel_log_hex64("    msi-capable funcs : ", (uint64_t)with_msi);
    kernel_log_ok("MSI-X foundation online");
    kernel_log_hex64("    msix-capable funcs: ", (uint64_t)with_msix);
}
