/* USB device registry (see usb_device.h, usb.h). */
#include "usb_device.h"
#include "log.h"

/* A small static pool keeps enumeration allocation-free and bounded. */
static struct usb_device g_devices[USB_MAX_DEVICES];
static int g_count;

const char *usb_speed_string(uint8_t speed) {
    switch (speed) {
    case 1:  return "full";
    case 2:  return "low";
    case 3:  return "high";
    case 4:  return "super";
    default: return "?";
    }
}

struct usb_device *usb_alloc_device(void) {
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (!g_devices[i].in_use) {
            struct usb_device *d = &g_devices[i];
            /* leave in_use clear until usb_register_device() */
            return d;
        }
    }
    return 0;
}

int usb_register_device(struct usb_device *device) {
    if (!device) {
        return -1;
    }
    if (!device->in_use) {
        device->in_use = 1;
        g_count++;
    }
    return 0;
}

int usb_device_count(void) {
    return g_count;
}

struct usb_device *usb_device_at(int index) {
    int seen = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (g_devices[i].in_use) {
            if (seen == index) {
                return &g_devices[i];
            }
            seen++;
        }
    }
    return 0;
}

void usb_dump_devices(void) {
    kernel_log_line("    usb devices:");
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        struct usb_device *d = &g_devices[i];
        if (!d->in_use) {
            continue;
        }
        kernel_log("      usb ");
        kernel_log_hex64("slot ", d->hc_slot);
        kernel_log_hex64("      vendor ", d->vendor_id);
        kernel_log_hex64("      product ", d->product_id);
        kernel_log("      class ");
        kernel_log_hex64("", d->config.interface.iface_class);
    }
}
