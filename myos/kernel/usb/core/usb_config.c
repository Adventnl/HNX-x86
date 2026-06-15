/* USB configuration helpers (see usb_config.h). */
#include "usb_config.h"

int usb_config_select(struct usb_device *dev, uint8_t value) {
    return usb_set_configuration(dev, value);
}

struct usb_interface *usb_device_interface(struct usb_device *dev) {
    return dev ? &dev->config.interface : 0;
}
