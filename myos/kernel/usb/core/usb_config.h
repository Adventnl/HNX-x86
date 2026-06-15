/* USB configuration selection helpers. */
#ifndef MYOS_USB_CONFIG_H
#define MYOS_USB_CONFIG_H

#include "types.h"
#include "usb.h"

/* Select a configuration by value (wraps usb_set_configuration). */
int usb_config_select(struct usb_device *dev, uint8_t value);

/* The device's first interface (boot devices expose exactly one). */
struct usb_interface *usb_device_interface(struct usb_device *dev);

#endif /* MYOS_USB_CONFIG_H */
