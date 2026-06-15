/* USB endpoint helpers (see usb_endpoint.h). */
#include "usb_endpoint.h"

int usb_endpoint_is_interrupt(const struct usb_endpoint *ep) {
    return ep && (ep->attributes & USB_EP_XFER_MASK) == USB_EP_INTERRUPT;
}

int usb_endpoint_is_in(const struct usb_endpoint *ep) {
    return ep && (ep->address & USB_EP_DIR_IN) != 0;
}

uint8_t usb_endpoint_number(const struct usb_endpoint *ep) {
    return ep ? (uint8_t)(ep->address & 0x0F) : 0;
}

uint8_t usb_endpoint_dci(const struct usb_endpoint *ep) {
    if (!ep) {
        return 0;
    }
    uint8_t n = ep->address & 0x0F;
    uint8_t in = (ep->address & USB_EP_DIR_IN) ? 1 : 0;
    return (uint8_t)(2 * n + in);
}

struct usb_endpoint *usb_interface_interrupt_in(struct usb_interface *iface) {
    if (!iface) {
        return 0;
    }
    for (uint8_t i = 0; i < iface->num_endpoints; i++) {
        struct usb_endpoint *ep = &iface->endpoints[i];
        if (usb_endpoint_is_interrupt(ep) && usb_endpoint_is_in(ep)) {
            return ep;
        }
    }
    return 0;
}
