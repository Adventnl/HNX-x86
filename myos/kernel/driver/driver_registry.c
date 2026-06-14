/* Driver/device registries (singly-linked lists) + generic probe. */
#include "driver_registry.h"
#include "string.h"
#include "log.h"

static struct driver *g_drivers;
static struct device *g_devices;
static struct device *g_devices_tail;
static int g_device_count;

void driver_registry_init(void) {
    g_drivers = NULL;
    g_devices = NULL;
    g_devices_tail = NULL;
    g_device_count = 0;
}

int driver_register(struct driver *driver) {
    if (!driver) {
        return -1;
    }
    driver->next = g_drivers;
    g_drivers = driver;
    return 0;
}

int device_register(struct device *device) {
    if (!device) {
        return -1;
    }
    device->next = NULL;
    if (g_devices_tail) {
        g_devices_tail->next = device;
    } else {
        g_devices = device;
    }
    g_devices_tail = device;
    g_device_count++;
    return 0;
}

int driver_probe_all(void) {
    int bound = 0;
    for (struct device *d = g_devices; d; d = d->next) {
        if (d->driver) {
            continue;
        }
        for (struct driver *drv = g_drivers; drv; drv = drv->next) {
            if (drv->type != d->type || !drv->probe) {
                continue;
            }
            if (drv->probe(d) == 0) {
                d->driver = drv;
                bound++;
                break;
            }
        }
    }
    return bound;
}

void device_dump_all(void) {
    kernel_log_line("    devices:");
    for (struct device *d = g_devices; d; d = d->next) {
        kernel_log("      ");
        kernel_log(d->name);
        kernel_log(" type=");
        kernel_log(device_type_name(d->type));
        kernel_log(" driver=");
        kernel_log_line(d->driver ? d->driver->name : "(none)");
    }
}

void driver_dump_all(void) {
    kernel_log_line("    drivers:");
    for (struct driver *drv = g_drivers; drv; drv = drv->next) {
        kernel_log("      ");
        kernel_log(drv->name);
        kernel_log(" (");
        kernel_log(device_type_name(drv->type));
        kernel_log_line(")");
    }
}

int device_count(void) {
    return g_device_count;
}

struct device *device_at(int index) {
    int i = 0;
    for (struct device *d = g_devices; d; d = d->next, i++) {
        if (i == index) {
            return d;
        }
    }
    return NULL;
}
