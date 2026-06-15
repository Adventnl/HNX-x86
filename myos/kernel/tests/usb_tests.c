/* USB core self-tests:
 *   - descriptor parser: a hand-built boot-keyboard configuration blob is run
 *     through the real usb_parse_config() and the extracted interface/endpoint
 *     fields are asserted.
 *   - enumeration: confirms at least one device was enumerated off the live xHCI
 *     root hub with a valid descriptor. */
#include "usb_tests.h"
#include "usb.h"
#include "usb_descriptor.h"
#include "usb_endpoint.h"
#include "log.h"

/* config(9) + interface(9) + HID(9) + endpoint(7) = 34 bytes. */
static const uint8_t k_boot_kbd_config[] = {
    /* configuration descriptor */
    0x09, USB_DT_CONFIG, 0x22, 0x00, 0x01, 0x01, 0x00, 0xA0, 0x32,
    /* interface descriptor: class 3 (HID), subclass 1 (boot), protocol 1 (kbd) */
    0x09, USB_DT_INTERFACE, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
    /* HID descriptor */
    0x09, USB_DT_HID, 0x11, 0x01, 0x00, 0x01, 0x22, 0x3F, 0x00,
    /* endpoint descriptor: EP1 IN, interrupt, 8 bytes, 10ms */
    0x07, USB_DT_ENDPOINT, 0x81, 0x03, 0x08, 0x00, 0x0A,
};

static int test_descriptor_parser(void) {
    struct usb_configuration cfg;
    if (usb_parse_config(k_boot_kbd_config, sizeof(k_boot_kbd_config), &cfg) != 0) {
        return -1;
    }
    if (cfg.num_interfaces != 1 || cfg.value != 1) {
        return -1;
    }
    if (cfg.interface.iface_class != 0x03 || cfg.interface.iface_subclass != 0x01 ||
        cfg.interface.iface_protocol != 0x01) {
        return -1;
    }
    if (cfg.interface.num_endpoints != 1) {
        return -1;
    }
    struct usb_endpoint *ep = usb_interface_interrupt_in(&cfg.interface);
    if (!ep || ep->address != 0x81 || ep->max_packet != 8) {
        return -1;
    }
    return 0;
}

void usb_tests_run(void) {
    if (test_descriptor_parser() != 0) {
        kernel_log_error("USB descriptor parser test failed");
        return;
    }
    kernel_log_line("[PASS] USB descriptor parser");

    /* Live enumeration off the xHCI root hub. */
    if (usb_device_count() <= 0) {
        kernel_log_error("usb enumeration: no devices enumerated");
        return;
    }
    struct usb_device *d = usb_device_at(0);
    if (!d || d->vendor_id == 0) {
        kernel_log_error("usb enumeration: device descriptor invalid");
        return;
    }
    kernel_log_line("[PASS] usb enumeration");
}
