/* USB core: bus + driver registries and driver matching (see usb.h). The device
 * registry lives in usb_device.c; the enumeration sequence in usb_request.c. */
#include "usb.h"
#include "log.h"

static struct usb_bus    *g_buses;
static struct usb_driver *g_drivers;

void usb_core_init(void) {
    g_buses = 0;
    g_drivers = 0;
    kernel_log_ok("USB core online");
}

int usb_register_bus(struct usb_bus *bus) {
    if (!bus) {
        return -1;
    }
    bus->next = g_buses;
    g_buses = bus;
    return 0;
}

int usb_register_driver(struct usb_driver *driver) {
    if (!driver) {
        return -1;
    }
    driver->next = g_drivers;
    g_drivers = driver;
    return 0;
}

void usb_match_drivers(void) {
    int n = usb_device_count();
    for (int i = 0; i < n; i++) {
        struct usb_device *d = usb_device_at(i);
        if (!d || d->driver) {
            continue;
        }
        for (struct usb_driver *drv = g_drivers; drv; drv = drv->next) {
            if (drv->probe && drv->probe(d) == 0) {
                d->driver = drv;
                break;
            }
        }
    }
}
