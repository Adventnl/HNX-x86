/* USB transfer helpers: the synchronous control pipe used by enumeration and
 * the descriptive usb_transfer record. */
#ifndef MYOS_USB_TRANSFER_H
#define MYOS_USB_TRANSFER_H

#include "types.h"
#include "usb.h"

/* Issue a control transfer on the device's default pipe via its bus. Returns 0
 * on success, negative on failure. */
int usb_control(struct usb_device *dev, uint8_t bmRequestType, uint8_t bRequest,
                uint16_t wValue, uint16_t wIndex, uint16_t wLength, void *data);

#endif /* MYOS_USB_TRANSFER_H */
