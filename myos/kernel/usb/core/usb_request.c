/* USB standard requests + the enumeration sequence (see usb_request.h, usb.h). */
#include "usb_request.h"
#include "usb.h"
#include "usb_transfer.h"
#include "usb_descriptor.h"
#include "hw_event_bus.h"
#include "string.h"
#include "log.h"

int usb_get_descriptor(struct usb_device *device, uint8_t type, uint8_t index,
                       void *buffer, uint16_t length) {
    return usb_control(device, USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                       USB_REQ_GET_DESCRIPTOR, (uint16_t)((type << 8) | index),
                       0, length, buffer);
}

int usb_set_address(struct usb_device *device, uint8_t address) {
    /* xHCI assigns the address via the Address Device command during slot setup;
     * the core only records the logical address here. */
    if (!device) {
        return -1;
    }
    device->address = address;
    return 0;
}

int usb_set_configuration(struct usb_device *device, uint8_t configuration_value) {
    int r = usb_control(device, USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                        USB_REQ_SET_CONFIGURATION, configuration_value, 0, 0, NULL);
    if (r == 0) {
        device->config.value = configuration_value;
    }
    return r;
}

int usb_enumerate_device(struct usb_device *device) {
    if (!device) {
        return -1;
    }
    struct usb_device_descriptor dd;
    memset(&dd, 0, sizeof(dd));

    /* First 8 bytes: confirm the default pipe + learn bMaxPacketSize0. */
    if (usb_get_descriptor(device, USB_DT_DEVICE, 0, &dd, 8) != 0) {
        kernel_log_error("usb: GET_DESCRIPTOR(device, 8) failed");
        return -1;
    }
    device->max_packet0 = dd.bMaxPacketSize0;

    /* Full 18-byte device descriptor. */
    if (usb_get_descriptor(device, USB_DT_DEVICE, 0, &dd, sizeof(dd)) != 0) {
        kernel_log_error("usb: GET_DESCRIPTOR(device, full) failed");
        return -1;
    }
    device->vendor_id = dd.idVendor;
    device->product_id = dd.idProduct;
    device->dev_class = dd.bDeviceClass;
    device->dev_subclass = dd.bDeviceSubClass;
    device->dev_protocol = dd.bDeviceProtocol;
    device->num_configs = dd.bNumConfigurations;

    /* Configuration descriptor: 9-byte header for wTotalLength, then the blob. */
    struct usb_config_descriptor cd;
    memset(&cd, 0, sizeof(cd));
    if (usb_get_descriptor(device, USB_DT_CONFIG, 0, &cd, sizeof(cd)) != 0) {
        kernel_log_error("usb: GET_DESCRIPTOR(config, 9) failed");
        return -1;
    }
    uint16_t total = cd.wTotalLength;
    if (total > USB_RAW_CONFIG_MAX) {
        total = USB_RAW_CONFIG_MAX;
    }
    if (usb_get_descriptor(device, USB_DT_CONFIG, 0, device->raw_config, total) != 0) {
        kernel_log_error("usb: GET_DESCRIPTOR(config, full) failed");
        return -1;
    }
    device->raw_config_len = total;

    if (usb_parse_config(device->raw_config, total, &device->config) != 0) {
        kernel_log_error("usb: config descriptor parse failed");
        return -1;
    }

    /* Select the (only) configuration. */
    if (usb_set_configuration(device, device->config.value) != 0) {
        kernel_log_error("usb: SET_CONFIGURATION failed");
        return -1;
    }

    hw_event_emit(HW_EVENT_USB_DEVICE_ATTACHED, device->vendor_id, device->product_id,
                  "usb device enumerated");
    return 0;
}
