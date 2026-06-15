/* Network interface registry + loopback. */
#include "net_if.h"
#include "ethernet.h"
#include "net_endian.h"
#include "string.h"
#include "log.h"
#include "fmt.h"

/* Upper-layer entry points (defined in arp.c / ipv4.c). Declared here to avoid
 * a header cycle through net_if.h. nb->data points past the Ethernet header. */
void arp_input(struct netif *nif, struct netbuf *nb,
               const struct mac_addr *src);
void ipv4_input(struct netif *nif, struct netbuf *nb);

static struct netif *g_netifs[NETIF_MAX];
static unsigned      g_netif_count;

void netif_registry_init(void) {
    for (unsigned i = 0; i < NETIF_MAX; i++) {
        g_netifs[i] = NULL;
    }
    g_netif_count = 0;
}

int netif_register(struct netif *nif) {
    if (nif == NULL || g_netif_count >= NETIF_MAX) {
        return -1;
    }
    if (nif->registered) {
        return 0;
    }
    g_netifs[g_netif_count++] = nif;
    nif->registered = 1;
    return 0;
}

struct netif *netif_get(const char *name) {
    for (unsigned i = 0; i < g_netif_count; i++) {
        if (strcmp(g_netifs[i]->name, name) == 0) {
            return g_netifs[i];
        }
    }
    return NULL;
}

struct netif *netif_get_by_index(unsigned idx) {
    if (idx >= g_netif_count) {
        return NULL;
    }
    return g_netifs[idx];
}

unsigned netif_count(void) {
    return g_netif_count;
}

struct netif *netif_default(void) {
    struct netif *lo = NULL;
    for (unsigned i = 0; i < g_netif_count; i++) {
        struct netif *n = g_netifs[i];
        if (!netif_is_up(n)) {
            continue;
        }
        if (n->flags & NETIF_FLAG_LOOPBACK) {
            lo = n;
            continue;
        }
        return n;
    }
    return lo;
}

struct netif *netif_for_dest(struct ip_addr dst) {
    for (unsigned i = 0; i < g_netif_count; i++) {
        struct netif *n = g_netifs[i];
        if (!netif_is_up(n) || ip_is_zero(n->ip)) {
            continue;
        }
        if (ip_same_subnet(dst, n->ip, n->netmask)) {
            return n;
        }
    }
    return NULL;
}

void netif_set_addr(struct netif *nif, struct ip_addr ip,
                    struct ip_addr mask, struct ip_addr gw) {
    nif->ip = ip;
    nif->netmask = mask;
    nif->gateway = gw;
}

void netif_up(struct netif *nif)   { nif->flags |= NETIF_FLAG_UP; }
void netif_down(struct netif *nif) { nif->flags &= ~NETIF_FLAG_UP; }

int netif_transmit(struct netif *nif, struct netbuf *nb) {
    if (nif == NULL || nif->tx == NULL) {
        if (nb) {
            netbuf_free(nb);
        }
        return -1;
    }
    if (!netif_is_up(nif)) {
        nif->stats.tx_dropped++;
        netbuf_free(nb);
        return -1;
    }
    size_t len = netbuf_len(nb);
    int rc = nif->tx(nif, nb);
    if (rc == 0) {
        nif->stats.tx_packets++;
        nif->stats.tx_bytes += len;
    } else {
        nif->stats.tx_errors++;
    }
    return rc;
}

void netif_rx(struct netif *nif, struct netbuf *nb) {
    if (nb == NULL) {
        return;
    }
    nif->stats.rx_packets++;
    nif->stats.rx_bytes += netbuf_len(nb);

    struct mac_addr dst, src;
    u16 ethertype = 0;
    if (eth_parse(nb, &dst, &src, &ethertype) != 0) {
        nif->stats.rx_errors++;
        netbuf_free(nb);
        return;
    }
    nb->netif = nif;

    switch (ethertype) {
    case ETH_P_ARP:
        arp_input(nif, nb, &src);
        netbuf_free(nb);
        break;
    case ETH_P_IPV4:
        ipv4_input(nif, nb);
        netbuf_free(nb);
        break;
    default:
        nif->stats.rx_dropped++;
        netbuf_free(nb);
        break;
    }
}

/* ---- Loopback ------------------------------------------------------------- */

static struct netif g_loopback;

static int loopback_tx(struct netif *nif, struct netbuf *nb) {
    /* Clone the framed bytes into a fresh buffer and feed them back to rx so
     * the receive path owns/frees its own copy independently of the caller. */
    size_t len = netbuf_len(nb);
    struct netbuf *rxb = netbuf_alloc(len);
    if (rxb == NULL) {
        return -1;
    }
    netbuf_put_data(rxb, nb->data, len);
    netif_rx(nif, rxb);
    return 0;
}

struct netif *loopback_init(void) {
    memset(&g_loopback, 0, sizeof(g_loopback));
    strlcpy(g_loopback.name, "lo", NETIF_NAME_MAX);
    g_loopback.mac = eth_zero;
    g_loopback.mtu = 65535;
    g_loopback.flags = NETIF_FLAG_LOOPBACK;
    g_loopback.tx = loopback_tx;
    netif_set_addr(&g_loopback, IP_ADDR_LOOPBACK,
                   ip_make(255, 0, 0, 0), IP_ADDR_ANY);
    netif_register(&g_loopback);
    netif_up(&g_loopback);
    return &g_loopback;
}

struct netif *loopback_get(void) {
    return &g_loopback;
}
