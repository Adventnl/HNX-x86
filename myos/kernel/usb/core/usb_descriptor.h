/* USB standard descriptor layouts + the parser that walks a configuration blob.
 * All multi-byte fields are little-endian (USB wire order == x86 native). */
#ifndef MYOS_USB_DESCRIPTOR_H
#define MYOS_USB_DESCRIPTOR_H

#include "types.h"

/* Descriptor types (bDescriptorType / GET_DESCRIPTOR wValue high byte). */
#define USB_DT_DEVICE        0x01
#define USB_DT_CONFIG        0x02
#define USB_DT_STRING        0x03
#define USB_DT_INTERFACE     0x04
#define USB_DT_ENDPOINT      0x05
#define USB_DT_HID           0x21
#define USB_DT_HID_REPORT    0x22

/* Endpoint attributes (bmAttributes transfer type). */
#define USB_EP_CONTROL       0x00
#define USB_EP_ISOCH         0x01
#define USB_EP_BULK          0x02
#define USB_EP_INTERRUPT     0x03
#define USB_EP_XFER_MASK     0x03
#define USB_EP_DIR_IN        0x80

struct usb_device_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed));

struct usb_config_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed));

struct usb_interface_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed));

struct usb_endpoint_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed));

struct usb_hid_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdHID;
    uint8_t  bCountryCode;
    uint8_t  bNumDescriptors;
    uint8_t  bReportDescriptorType;
    uint16_t wReportDescriptorLength;
} __attribute__((packed));

const char *usb_descriptor_type_name(uint8_t type);

struct usb_configuration;

/* Walk a configuration descriptor blob (config + interface + endpoint
 * descriptors) and fill `out` with the first interface and its endpoints.
 * Returns 0 on success, -1 if the blob is malformed. */
int usb_parse_config(const uint8_t *blob, uint16_t len, struct usb_configuration *out);

#endif /* MYOS_USB_DESCRIPTOR_H */
