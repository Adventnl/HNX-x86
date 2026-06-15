/* USB core: the host-controller-agnostic device model and enumeration API.
 *
 * A bus (registered by a host controller such as xHCI) provides the default
 * control pipe and endpoint-configuration hooks. The core fetches descriptors,
 * selects a configuration, and offers the resulting devices to USB drivers
 * (e.g. HID). xHCI assigns device addresses in hardware, so usb_set_address()
 * records the logical address rather than issuing SET_ADDRESS. */
#ifndef MYOS_USB_H
#define MYOS_USB_H

#include "types.h"
#include "usb_descriptor.h"

#define USB_MAX_DEVICES     16
#define USB_MAX_ENDPOINTS   8
#define USB_RAW_CONFIG_MAX  256

struct usb_device;
struct usb_bus;

/* Parsed endpoint. */
struct usb_endpoint {
    uint8_t  address;        /* bEndpointAddress (incl. dir bit)  */
    uint8_t  attributes;     /* bmAttributes (transfer type)      */
    uint16_t max_packet;     /* wMaxPacketSize                    */
    uint8_t  interval;       /* bInterval                         */
    uint8_t  in_use;
};

/* Parsed interface (first alternate setting). */
struct usb_interface {
    uint8_t number;
    uint8_t iface_class;
    uint8_t iface_subclass;
    uint8_t iface_protocol;
    uint8_t num_endpoints;
    struct usb_endpoint endpoints[USB_MAX_ENDPOINTS];
};

/* Active configuration. */
struct usb_configuration {
    uint8_t  value;
    uint8_t  num_interfaces;
    uint16_t total_length;
    struct usb_interface interface;   /* first interface (boot devices) */
};

/* Host-controller bus: supplies the default control pipe + endpoint setup. */
struct usb_bus {
    const char *name;
    void       *hc;          /* host controller (struct xhci *)   */
    int (*control)(struct usb_device *dev, uint8_t bmRequestType, uint8_t bRequest,
                   uint16_t wValue, uint16_t wIndex, uint16_t wLength, void *data);
    /* Configure a periodic IN endpoint on the device's slot. Returns 0. */
    int (*configure_endpoint)(struct usb_device *dev, uint8_t ep_addr,
                              uint16_t max_packet, uint8_t interval);
    /* Queue an interrupt-IN transfer into `dma_buf` (identity-mapped). */
    int (*submit_interrupt)(struct usb_device *dev, uint64_t dma_buf, uint16_t length);
    /* Non-blocking check for a completed interrupt transfer. Returns 1 if a
     * report arrived (and re-arming is the caller's job), else 0. */
    int (*poll_interrupt)(struct usb_device *dev, int *bytes);
    struct usb_bus *next;
};

struct usb_device {
    struct usb_bus *bus;
    uint8_t  address;
    uint8_t  hc_slot;        /* xHCI slot id                      */
    uint8_t  port;           /* root hub port                     */
    uint8_t  speed;

    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  dev_class, dev_subclass, dev_protocol;
    uint8_t  max_packet0;
    uint8_t  num_configs;

    struct usb_configuration config;

    struct usb_driver *driver;
    void              *driver_data;

    uint8_t  raw_config[USB_RAW_CONFIG_MAX];
    uint16_t raw_config_len;
    uint8_t  in_use;
};

/* A USB class driver (HID etc.). probe() returns 0 to claim the device. */
struct usb_driver {
    const char *name;
    int (*probe)(struct usb_device *dev);
    struct usb_driver *next;
};

/* Logical transfer record (descriptive; control transfers run synchronously). */
struct usb_transfer {
    struct usb_device *dev;
    uint8_t  endpoint;
    uint8_t  direction_in;
    void    *buffer;
    uint16_t length;
    int      status;
};

/* ---- core API ------------------------------------------------------------- */
void usb_core_init(void);
int  usb_register_bus(struct usb_bus *bus);
int  usb_register_device(struct usb_device *device);
int  usb_enumerate_device(struct usb_device *device);
int  usb_get_descriptor(struct usb_device *device, uint8_t type, uint8_t index,
                        void *buffer, uint16_t length);
int  usb_set_address(struct usb_device *device, uint8_t address);
int  usb_set_configuration(struct usb_device *device, uint8_t configuration_value);
void usb_dump_devices(void);

int  usb_register_driver(struct usb_driver *driver);
void usb_match_drivers(void);

/* Device registry. */
struct usb_device *usb_alloc_device(void);
int                usb_device_count(void);
struct usb_device *usb_device_at(int index);

#endif /* MYOS_USB_H */
