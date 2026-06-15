/* USB descriptor parsing (see usb_descriptor.h). */
#include "usb_descriptor.h"
#include "usb.h"
#include "string.h"

const char *usb_descriptor_type_name(uint8_t type) {
    switch (type) {
    case USB_DT_DEVICE:     return "device";
    case USB_DT_CONFIG:     return "config";
    case USB_DT_STRING:     return "string";
    case USB_DT_INTERFACE:  return "interface";
    case USB_DT_ENDPOINT:   return "endpoint";
    case USB_DT_HID:        return "hid";
    case USB_DT_HID_REPORT: return "hid-report";
    default:                return "unknown";
    }
}

int usb_parse_config(const uint8_t *blob, uint16_t len, struct usb_configuration *out) {
    if (!blob || !out || len < (uint16_t)sizeof(struct usb_config_descriptor)) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    const struct usb_config_descriptor *cfg = (const struct usb_config_descriptor *)blob;
    if (cfg->bDescriptorType != USB_DT_CONFIG) {
        return -1;
    }
    out->value = cfg->bConfigurationValue;
    out->num_interfaces = cfg->bNumInterfaces;
    out->total_length = cfg->wTotalLength;

    int have_iface = 0;
    uint16_t off = cfg->bLength;
    while (off + 2 <= len) {
        uint8_t blen = blob[off];
        uint8_t btype = blob[off + 1];
        if (blen == 0 || off + blen > len) {
            break;
        }
        if (btype == USB_DT_INTERFACE && !have_iface) {
            const struct usb_interface_descriptor *id =
                (const struct usb_interface_descriptor *)(blob + off);
            out->interface.number = id->bInterfaceNumber;
            out->interface.iface_class = id->bInterfaceClass;
            out->interface.iface_subclass = id->bInterfaceSubClass;
            out->interface.iface_protocol = id->bInterfaceProtocol;
            out->interface.num_endpoints = 0;
            have_iface = 1;
        } else if (btype == USB_DT_ENDPOINT && have_iface) {
            const struct usb_endpoint_descriptor *ed =
                (const struct usb_endpoint_descriptor *)(blob + off);
            uint8_t n = out->interface.num_endpoints;
            if (n < USB_MAX_ENDPOINTS) {
                out->interface.endpoints[n].address = ed->bEndpointAddress;
                out->interface.endpoints[n].attributes = ed->bmAttributes;
                out->interface.endpoints[n].max_packet = ed->wMaxPacketSize;
                out->interface.endpoints[n].interval = ed->bInterval;
                out->interface.endpoints[n].in_use = 1;
                out->interface.num_endpoints = (uint8_t)(n + 1);
            }
        }
        off = (uint16_t)(off + blen);
    }
    return have_iface ? 0 : -1;
}
