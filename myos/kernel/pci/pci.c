/* PCI enumeration (mechanism #1). Scans bus 0-255 / slot 0-31 / func 0-7 and
 * records every present function into a flat table. */
#include "pci.h"
#include "pci_config.h"
#include "pci_ids.h"
#include "driver.h"
#include "driver_registry.h"
#include "heap.h"
#include "log.h"

static struct pci_device g_devices[PCI_MAX_DEVICES];
static int g_count;

static void read_function(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t vendor = pci_config_read16(bus, slot, func, 0x00);
    if (vendor == 0xFFFF) {
        return;
    }
    if (g_count >= PCI_MAX_DEVICES) {
        return;
    }
    struct pci_device *d = &g_devices[g_count];
    d->bus = bus;
    d->slot = slot;
    d->func = func;
    d->vendor = vendor;
    d->device = pci_config_read16(bus, slot, func, 0x02);
    d->revision = pci_config_read8(bus, slot, func, 0x08);
    d->prog_if = pci_config_read8(bus, slot, func, 0x09);
    d->subclass = pci_config_read8(bus, slot, func, 0x0A);
    d->class_code = pci_config_read8(bus, slot, func, 0x0B);
    d->header_type = pci_config_read8(bus, slot, func, 0x0E);
    for (int i = 0; i < 6; i++) {
        d->bar[i] = pci_config_read32(bus, slot, func, (uint8_t)(0x10 + i * 4));
    }
    d->irq_line = pci_config_read8(bus, slot, func, 0x3C);
    d->irq_pin = pci_config_read8(bus, slot, func, 0x3D);
    d->in_use = 1;
    g_count++;
}

void pci_scan_all(void) {
    g_count = 0;
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint16_t vendor = pci_config_read16((uint8_t)bus, (uint8_t)slot, 0, 0x00);
            if (vendor == 0xFFFF) {
                continue;
            }
            read_function((uint8_t)bus, (uint8_t)slot, 0);
            uint8_t header = pci_config_read8((uint8_t)bus, (uint8_t)slot, 0, 0x0E);
            if (header & 0x80) {                 /* multi-function device */
                for (int func = 1; func < 8; func++) {
                    read_function((uint8_t)bus, (uint8_t)slot, (uint8_t)func);
                }
            }
        }
    }
}

void pci_init(void) {
    pci_scan_all();
    kernel_log_ok("PCI bus scanned");
    kernel_log_hex64("    pci functions  : ", (uint64_t)g_count);
}

const struct pci_device *pci_find_by_class(uint8_t class_code, uint8_t subclass) {
    for (int i = 0; i < g_count; i++) {
        if (g_devices[i].class_code == class_code && g_devices[i].subclass == subclass) {
            return &g_devices[i];
        }
    }
    return NULL;
}

const struct pci_device *pci_find_by_class_prog(uint8_t class_code, uint8_t subclass,
                                                uint8_t prog_if) {
    for (int i = 0; i < g_count; i++) {
        if (g_devices[i].class_code == class_code && g_devices[i].subclass == subclass &&
            g_devices[i].prog_if == prog_if) {
            return &g_devices[i];
        }
    }
    return NULL;
}

const struct pci_device *pci_find_by_id(uint16_t vendor, uint16_t device) {
    for (int i = 0; i < g_count; i++) {
        if (g_devices[i].vendor == vendor && g_devices[i].device == device) {
            return &g_devices[i];
        }
    }
    return NULL;
}

int pci_device_count(void) {
    return g_count;
}

const struct pci_device *pci_device_at(int index) {
    if (index < 0 || index >= g_count) {
        return NULL;
    }
    return &g_devices[index];
}

static char hex_digit(int v) {
    return (char)(v < 10 ? '0' + v : 'a' + v - 10);
}

void pci_register_devices(void) {
    for (int i = 0; i < g_count; i++) {
        struct pci_device *p = &g_devices[i];
        struct device *d = (struct device *)kcalloc(1, sizeof(*d));
        if (!d) {
            return;
        }
        char *n = d->name;
        int k = 0;
        n[k++] = 'p'; n[k++] = 'c'; n[k++] = 'i';
        n[k++] = hex_digit((p->bus >> 4) & 0xF); n[k++] = hex_digit(p->bus & 0xF);
        n[k++] = ':';
        n[k++] = hex_digit((p->slot >> 4) & 0xF); n[k++] = hex_digit(p->slot & 0xF);
        n[k++] = '.';
        n[k++] = hex_digit(p->func & 0xF);
        n[k] = 0;
        d->type = DEV_TYPE_PCI;
        d->id.vendor = p->vendor;
        d->id.device = p->device;
        d->id.class_code = p->class_code;
        d->id.subclass = p->subclass;
        d->id.prog_if = p->prog_if;
        d->bus_data = p;
        device_register(d);
    }
}

void pci_dump_devices(void) {
    for (int i = 0; i < g_count; i++) {
        struct pci_device *d = &g_devices[i];
        kernel_log("    ");
        kernel_log_hex64("pci ", ((uint64_t)d->bus << 16) | ((uint64_t)d->slot << 8) | d->func);
        kernel_log("      ");
        kernel_log(pci_class_name(d->class_code, d->subclass));
        kernel_log_hex64("  vendor=", d->vendor);
        kernel_log_hex64("      device=", d->device);
    }
}
