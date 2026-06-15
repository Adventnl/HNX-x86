/* USB hub foundation (see usb_hub.h). */
#include "usb_hub.h"
#include "string.h"
#include "log.h"

static struct usb_hub g_root_hub;
static int g_have_root;

void usb_hub_init(void) {
    memset(&g_root_hub, 0, sizeof(g_root_hub));
    g_have_root = 0;
    kernel_log_ok("USB hub foundation online");
}

struct usb_hub *usb_hub_register_root(const char *name, uint8_t num_ports) {
    memset(&g_root_hub, 0, sizeof(g_root_hub));
    g_root_hub.name = name;
    g_root_hub.is_root = 1;
    g_root_hub.num_ports = (num_ports > USB_HUB_MAX_PORTS) ? USB_HUB_MAX_PORTS : num_ports;
    g_have_root = 1;
    return &g_root_hub;
}

void usb_hub_set_port(struct usb_hub *hub, uint8_t port, uint8_t connected, uint8_t speed) {
    if (!hub || port == 0 || port > hub->num_ports) {
        return;
    }
    hub->ports[port - 1].connected = connected;
    hub->ports[port - 1].speed = speed;
}

struct usb_hub *usb_hub_root(void) {
    return g_have_root ? &g_root_hub : 0;
}

uint8_t usb_hub_connected_count(struct usb_hub *hub) {
    if (!hub) {
        return 0;
    }
    uint8_t n = 0;
    for (uint8_t i = 0; i < hub->num_ports; i++) {
        if (hub->ports[i].connected) {
            n++;
        }
    }
    return n;
}
