/* MSI / MSI-X capability self-tests. Exercises the real capability walker, the
 * MSI enable/disable round-trip (left disabled afterwards so no vector is armed)
 * and the MSI-X table MMIO mapping against whatever capable functions QEMU
 * presents (AHCI, NVMe and the xHCI controller are all MSI/MSI-X capable). */
#include "msi_tests.h"
#include "msi.h"
#include "msix.h"
#include "pci_caps.h"
#include "pci.h"
#include "pci_device.h"
#include "log.h"

void msi_tests_run(void) {
    int functions = pci_device_count();
    int with_caps = 0;
    struct pci_device *msi_dev = 0, *msix_dev = 0;

    for (int i = 0; i < functions; i++) {
        struct pci_device *d = (struct pci_device *)pci_device_at(i);
        if (!d) {
            continue;
        }
        if (pci_find_capability(d, PCI_CAP_ID_PM) >= 0 ||
            pci_find_capability(d, PCI_CAP_ID_MSI) >= 0 ||
            pci_find_capability(d, PCI_CAP_ID_MSIX) >= 0 ||
            pci_find_capability(d, PCI_CAP_ID_PCIE) >= 0) {
            with_caps++;
        }
        if (!msi_dev && msi_supported(d)) {
            msi_dev = d;
        }
        if (!msix_dev && msix_supported(d)) {
            msix_dev = d;
        }
    }

    if (with_caps == 0) {
        kernel_log_error("msi capability tests: no capability lists found");
        return;
    }

    /* MSI enable/disable round-trip on a real function. */
    if (msi_dev) {
        if (msi_enable(msi_dev, 0x71) != 0 || msi_is_enabled(msi_dev) != 1) {
            kernel_log_error("msi capability tests: enable round-trip failed");
            return;
        }
        msi_disable(msi_dev);
        if (msi_is_enabled(msi_dev) != 0) {
            kernel_log_error("msi capability tests: disable round-trip failed");
            return;
        }
    }

    /* MSI-X table size + MMIO table mapping on a real function. */
    if (msix_dev) {
        if (msix_table_size(msix_dev) <= 0 || msix_map_table(msix_dev) != 0) {
            kernel_log_error("msi capability tests: msix table map failed");
            return;
        }
    }

    kernel_log_line("[PASS] msi capability tests");
}
