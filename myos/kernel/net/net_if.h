/* Network interface abstraction + interface registry.
 *
 * A netif binds a link-layer device (loopback, e1000, ...) to the protocol
 * stack. The driver supplies a tx callback; received frames are pushed up via
 * netif_rx() which dispatches by EtherType. A small registry tracks all
 * interfaces so routing/ARP can find one by name or address.
 */
#ifndef MYOS_NET_IF_H
#define MYOS_NET_IF_H

#include "types.h"
#include "ethernet.h"
#include "net_addr.h"
#include "netbuf.h"

#define NETIF_NAME_MAX 8u
#define NETIF_MAX      4u

struct netif;

/* Driver transmit hook: hand a fully framed Ethernet frame (nb->data points at
 * the Ethernet header) to the link layer. Returns 0 on success. */
typedef int (*netif_tx_fn)(struct netif *nif, struct netbuf *nb);

struct netif_stats {
    u64 rx_packets;
    u64 tx_packets;
    u64 rx_bytes;
    u64 tx_bytes;
    u64 rx_dropped;
    u64 tx_dropped;
    u64 rx_errors;
    u64 tx_errors;
};

#define NETIF_FLAG_UP       0x01u
#define NETIF_FLAG_LOOPBACK 0x02u
#define NETIF_FLAG_BROADCAST 0x04u

struct netif {
    char             name[NETIF_NAME_MAX];
    struct mac_addr  mac;
    struct ip_addr   ip;
    struct ip_addr   netmask;
    struct ip_addr   gateway;
    u16              mtu;
    u32              flags;
    netif_tx_fn      tx;
    void            *driver;        /* driver private state */
    struct netif_stats stats;
    int              registered;
};

/* Registry. */
void          netif_registry_init(void);
int           netif_register(struct netif *nif);
struct netif *netif_get(const char *name);
struct netif *netif_get_by_index(unsigned idx);
unsigned      netif_count(void);
/* The first UP, non-loopback interface, else loopback, else NULL. */
struct netif *netif_default(void);
/* Find the interface whose subnet contains `dst`, else NULL. */
struct netif *netif_for_dest(struct ip_addr dst);

void netif_set_addr(struct netif *nif, struct ip_addr ip,
                    struct ip_addr mask, struct ip_addr gw);
void netif_up(struct netif *nif);
void netif_down(struct netif *nif);
static inline int netif_is_up(const struct netif *nif) {
    return (nif->flags & NETIF_FLAG_UP) != 0;
}

/* Transmit a framed packet through the interface (updates stats). */
int  netif_transmit(struct netif *nif, struct netbuf *nb);

/* Receive an Ethernet frame from the driver: parses the header and dispatches
 * to ARP / IPv4. Consumes (frees) nb. */
void netif_rx(struct netif *nif, struct netbuf *nb);

/* ---- Loopback ------------------------------------------------------------- */
/* Create + register "lo" (127.0.0.1/8). Its tx loops the frame back into rx. */
struct netif *loopback_init(void);
struct netif *loopback_get(void);

#endif /* MYOS_NET_IF_H */
