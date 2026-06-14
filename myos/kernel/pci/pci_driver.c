/* PCI driver registry + matcher. */
#include "pci_driver.h"
#include "pci.h"
#include "pci_device.h"

static struct pci_driver *g_drivers;

void pci_driver_register(struct pci_driver *drv) {
    if (!drv) {
        return;
    }
    drv->next = g_drivers;
    g_drivers = drv;
}

static int matches(const struct pci_driver *drv, const struct pci_device *dev) {
    if (drv->class_code != dev->class_code || drv->subclass != dev->subclass) {
        return 0;
    }
    if (drv->match_prog_if && drv->prog_if != dev->prog_if) {
        return 0;
    }
    return 1;
}

int pci_driver_match_all(void) {
    int bound = 0;
    int n = pci_device_count();
    for (int i = 0; i < n; i++) {
        const struct pci_device *dev = pci_device_at(i);
        for (struct pci_driver *drv = g_drivers; drv; drv = drv->next) {
            if (matches(drv, dev) && drv->probe && drv->probe(dev) == 0) {
                bound++;
                break;
            }
        }
    }
    return bound;
}
