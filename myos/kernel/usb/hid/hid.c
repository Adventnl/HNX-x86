/* USB HID core (see hid.h). */
#include "hid.h"
#include "hid_keyboard.h"
#include "hid_mouse.h"
#include "hid_report.h"
#include "usb.h"
#include "usb_request.h"
#include "usb_transfer.h"
#include "usb_endpoint.h"
#include "hw_event_bus.h"
#include "pmm.h"
#include "memory_layout.h"
#include "string.h"
#include "log.h"

#define HID_INTERFACE_CLASS 0x03

static struct hid_device g_hids[USB_MAX_DEVICES];

static struct hid_device *alloc_hid(void) {
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (!g_hids[i].in_use) {
            return &g_hids[i];
        }
    }
    return 0;
}

int hid_device_count(void) {
    int n = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (g_hids[i].in_use) {
            n++;
        }
    }
    return n;
}

struct hid_device *hid_device_at(int index) {
    int seen = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (g_hids[i].in_use) {
            if (seen == index) {
                return &g_hids[i];
            }
            seen++;
        }
    }
    return 0;
}

static int hid_probe(struct usb_device *dev) {
    struct usb_interface *iface = &dev->config.interface;
    if (iface->iface_class != HID_INTERFACE_CLASS) {
        return -1;                       /* not a HID interface */
    }
    struct usb_endpoint *ep = usb_interface_interrupt_in(iface);
    if (!ep) {
        return -1;
    }
    struct hid_device *hd = alloc_hid();
    if (!hd) {
        return -1;
    }
    hd->usb = dev;
    hd->in_use = 1;
    hd->intr_ep = ep->address;
    hd->intr_mps = ep->max_packet;
    hd->interval = ep->interval;
    /* boot protocol: interface protocol 1 = keyboard, 2 = mouse. */
    hd->type = (iface->iface_protocol == 2) ? HID_TYPE_MOUSE : HID_TYPE_KEYBOARD;
    dev->driver_data = hd;

    /* Switch the interface to the boot protocol and disable idle reporting. */
    usb_control(dev, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                HID_REQ_SET_PROTOCOL, 0 /*boot*/, iface->number, 0, NULL);
    usb_control(dev, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                HID_REQ_SET_IDLE, 0, iface->number, 0, NULL);

    /* Configure the interrupt-IN endpoint and arm one transfer. */
    if (dev->bus->configure_endpoint) {
        dev->bus->configure_endpoint(dev, ep->address, ep->max_packet, ep->interval);
    }
    hd->report_buf = pmm_alloc_page();
    if (hd->report_buf != PMM_INVALID_PAGE) {
        memset((void *)(uintptr_t)hd->report_buf, 0, PAGE_SIZE);
        if (dev->bus->submit_interrupt) {
            dev->bus->submit_interrupt(dev, hd->report_buf, hd->intr_mps);
        }
    }

    if (hd->type == HID_TYPE_KEYBOARD) {
        hid_keyboard_attach(dev);
    } else {
        hid_mouse_attach(dev);
    }
    hw_event_emit(HW_EVENT_USB_DEVICE_ATTACHED, dev->vendor_id, hd->type, "usb-hid bound");
    return 0;
}

static struct usb_driver g_hid_driver = {
    .name = "usb-hid",
    .probe = hid_probe,
};

void hid_init(void) {
    memset(g_hids, 0, sizeof(g_hids));
    usb_register_driver(&g_hid_driver);
    kernel_log_ok("USB HID core online");
}

void hid_poll(void) {
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        struct hid_device *hd = &g_hids[i];
        if (!hd->in_use || !hd->usb || !hd->usb->bus->poll_interrupt) {
            continue;
        }
        int bytes = 0;
        if (hd->usb->bus->poll_interrupt(hd->usb, &bytes)) {
            const uint8_t *report = (const uint8_t *)(uintptr_t)hd->report_buf;
            if (hd->type == HID_TYPE_KEYBOARD) {
                hid_keyboard_handle_report(hd->usb, report, hd->intr_mps);
            } else {
                hid_mouse_handle_report(hd->usb, report, hd->intr_mps);
            }
            /* Re-arm the transfer. */
            if (hd->usb->bus->submit_interrupt) {
                hd->usb->bus->submit_interrupt(hd->usb, hd->report_buf, hd->intr_mps);
            }
        }
    }
}
