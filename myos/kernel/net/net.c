/* Networking subsystem bring-up. */
#include "net.h"
#include "log.h"
#include "fmt.h"

void net_init(void) {
    /* Core registries / protocol state. */
    netif_registry_init();
    arp_init();
    route_init();
    ipv4_init();
    icmp_init();
    udp_init();
    tcp_init();
    dns_init();
    net_socket_init();

    /* Loopback first: always available for the local stack + tests. */
    struct netif *lo = loopback_init();
    if (lo != NULL) {
        /* Route the loopback /8 (127.0.0.0/8) through "lo". */
        route_add(ip_make(127, 0, 0, 0), ip_make(255, 0, 0, 0),
                  IP_ADDR_ANY, lo);
        kernel_log_ok("Loopback interface online");
    }

    /* Probe the e1000 NIC. Absent in a pure-loopback environment; that is
     * fine, the protocol tests run over loopback. */
    if (e1000_init() == 0) {
        struct e1000_device *dev = e1000_primary();
        if (dev != NULL) {
            /* A static address + default route for the NIC (no DHCP yet). */
            netif_set_addr(&dev->nif, ip_make(10, 0, 2, 15),
                           ip_make(255, 255, 255, 0), ip_make(10, 0, 2, 2));
            netif_up(&dev->nif);
            route_add(ip_make(10, 0, 2, 0), ip_make(255, 255, 255, 0),
                      IP_ADDR_ANY, &dev->nif);
            route_add(IP_ADDR_ANY, IP_ADDR_ANY, ip_make(10, 0, 2, 2),
                      &dev->nif);   /* default route via gateway */
            kernel_log_ok("e1000 NIC online");
        }
    } else {
        kernel_log_line("    e1000 NIC not present (loopback-only)");
    }

    kernel_log_ok("Networking subsystem online");
}
