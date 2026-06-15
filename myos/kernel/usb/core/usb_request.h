/* USB standard device requests (Chapter 9). */
#ifndef MYOS_USB_REQUEST_H
#define MYOS_USB_REQUEST_H

#include "types.h"

/* bmRequestType direction/type. */
#define USB_DIR_OUT          0x00
#define USB_DIR_IN           0x80
#define USB_TYPE_STANDARD    0x00
#define USB_TYPE_CLASS       0x20
#define USB_RECIP_DEVICE     0x00
#define USB_RECIP_INTERFACE  0x01

/* bRequest (standard). */
#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_SET_INTERFACE     0x0B

#endif /* MYOS_USB_REQUEST_H */
