/* USB device registry helpers (the registry API itself lives in usb.h). */
#ifndef MYOS_USB_DEVICE_H
#define MYOS_USB_DEVICE_H

#include "types.h"
#include "usb.h"

const char *usb_speed_string(uint8_t speed);

#endif /* MYOS_USB_DEVICE_H */
