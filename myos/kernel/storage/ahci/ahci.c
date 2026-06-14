/* AHCI top level: PCI driver registration + probe. */
#include "ahci.h"
#include "ahci_controller.h"
#include "pci.h"
#include "pci_device.h"
#include "pci_driver.h"
#include "log.h"

static int ahci_probe(const struct pci_device *dev) {
    pci_device_enable(dev);
    int is_mmio = 0;
    uint64_t abar = pci_device_bar(dev, 5, &is_mmio);   /* ABAR is BAR5 */
    if (!abar || !is_mmio) {
        return -1;
    }
    kernel_log_ok("AHCI controller found");
    kernel_log_hex64("    abar           : ", abar);
    ahci_controller_init(abar);
    return 0;   /* claim the controller */
}

static struct pci_driver g_ahci_driver = {
    .name = "ahci",
    .class_code = 0x01,
    .subclass = 0x06,
    .prog_if = 0x01,
    .match_prog_if = 1,
    .probe = ahci_probe,
};

void ahci_init(void) {
    pci_driver_register(&g_ahci_driver);
}
