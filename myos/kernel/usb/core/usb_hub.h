/* USB hub foundation. MyOS models the xHCI root hub explicitly (port count +
 * per-port connection/speed) and provides the structure for external hubs to be
 * layered on later. */
#ifndef MYOS_USB_HUB_H
#define MYOS_USB_HUB_H

#include "types.h"

#define USB_HUB_MAX_PORTS 16

struct usb_hub_port {
    uint8_t connected;
    uint8_t speed;
    uint8_t reset_done;
};

struct usb_hub {
    const char *name;
    uint8_t     num_ports;
    uint8_t     is_root;
    struct usb_hub_port ports[USB_HUB_MAX_PORTS];
};

void  usb_hub_init(void);
/* Register the controller root hub with `num_ports` ports. Returns the hub. */
struct usb_hub *usb_hub_register_root(const char *name, uint8_t num_ports);
void  usb_hub_set_port(struct usb_hub *hub, uint8_t port, uint8_t connected, uint8_t speed);
struct usb_hub *usb_hub_root(void);
uint8_t usb_hub_connected_count(struct usb_hub *hub);

#endif /* MYOS_USB_HUB_H */
