/* USB transfer helpers (see usb_transfer.h). */
#include "usb_transfer.h"

int usb_control(struct usb_device *dev, uint8_t bmRequestType, uint8_t bRequest,
                uint16_t wValue, uint16_t wIndex, uint16_t wLength, void *data) {
    if (!dev || !dev->bus || !dev->bus->control) {
        return -1;
    }
    return dev->bus->control(dev, bmRequestType, bRequest, wValue, wIndex, wLength, data);
}
