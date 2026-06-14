/* NVMe top level: PCI driver registration + probe (class 1, subclass 8). */
#include "nvme.h"
#include "nvme_controller.h"
#include "pci.h"
#include "pci_device.h"
#include "pci_driver.h"
#include "log.h"

static int nvme_probe(const struct pci_device *dev) {
    pci_device_enable(dev);
    int is_mmio = 0;
    uint64_t bar0 = pci_device_bar(dev, 0, &is_mmio);   /* NVMe regs are BAR0 */
    if (!bar0 || !is_mmio) {
        return -1;
    }
    struct nvme_controller ctrl;
    if (nvme_controller_init(bar0, &ctrl) != 0) {
        return -1;
    }
    return 0;
}

static struct pci_driver g_nvme_driver = {
    .name = "nvme",
    .class_code = 0x01,
    .subclass = 0x08,
    .prog_if = 0x02,
    .match_prog_if = 0,    /* match any NVMe prog_if */
    .probe = nvme_probe,
};

void nvme_init(void) {
    pci_driver_register(&g_nvme_driver);
}
