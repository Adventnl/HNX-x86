/* USB endpoint helpers. */
#ifndef MYOS_USB_ENDPOINT_H
#define MYOS_USB_ENDPOINT_H

#include "types.h"
#include "usb.h"

int usb_endpoint_is_interrupt(const struct usb_endpoint *ep);
int usb_endpoint_is_in(const struct usb_endpoint *ep);
uint8_t usb_endpoint_number(const struct usb_endpoint *ep);

/* xHCI Device Context Index for an endpoint: 2*N + dir(IN=1). EP0 == DCI 1. */
uint8_t usb_endpoint_dci(const struct usb_endpoint *ep);

/* Find the first interrupt-IN endpoint of an interface, or NULL. */
struct usb_endpoint *usb_interface_interrupt_in(struct usb_interface *iface);

#endif /* MYOS_USB_ENDPOINT_H */
