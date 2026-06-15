/* IPv4 implementation: header build/parse, routing, input/output. */
#include "ipv4.h"
#include "ethernet.h"
#include "arp.h"
#include "checksum.h"
#include "net_endian.h"
#include "string.h"
#include "log.h"

static struct ip_route g_routes[IP_ROUTE_MAX];
static unsigned        g_route_count;
static u16             g_ip_id;

/* Count contiguous leading one-bits in a network-order mask. */
static u32 mask_prefix_len(struct ip_addr mask) {
    u32 host = ip_to_host(mask);
    u32 n = 0;
    for (int i = 31; i >= 0; i--) {
        if (host & (1u << i)) {
            n++;
        } else {
            break;
        }
    }
    return n;
}

void route_init(void) {
    for (unsigned i = 0; i < IP_ROUTE_MAX; i++) {
        g_routes[i].valid = 0;
    }
    g_route_count = 0;
}

int route_add(struct ip_addr dest, struct ip_addr netmask,
              struct ip_addr gateway, struct netif *nif) {
    if (g_route_count >= IP_ROUTE_MAX) {
        return -1;
    }
    struct ip_route *r = &g_routes[g_route_count++];
    r->dest = dest;
    r->netmask = netmask;
    r->gateway = gateway;
    r->nif = nif;
    r->prefix_len = mask_prefix_len(netmask);
    r->valid = 1;
    return 0;
}

unsigned route_count(void) {
    return g_route_count;
}

struct ip_route *route_lookup(struct ip_addr dst, struct ip_addr *next_hop) {
    struct ip_route *best = NULL;
    for (unsigned i = 0; i < g_route_count; i++) {
        struct ip_route *r = &g_routes[i];
        if (!r->valid) {
            continue;
        }
        if ((dst.be & r->netmask.be) == (r->dest.be & r->netmask.be)) {
            if (best == NULL || r->prefix_len > best->prefix_len) {
                best = r;
            }
        }
    }
    if (best == NULL) {
        return NULL;
    }
    if (next_hop != NULL) {
        *next_hop = ip_is_zero(best->gateway) ? dst : best->gateway;
    }
    return best;
}

void ipv4_init(void) {
    g_ip_id = 0x1000;
}

int ipv4_build(struct netbuf *nb, struct ip_addr src, struct ip_addr dst,
               u8 proto, u8 ttl) {
    u16 payload = (u16)netbuf_len(nb);
    struct ipv4_header *h =
        (struct ipv4_header *)netbuf_push(nb, IPV4_HDR_LEN);
    if (h == NULL) {
        return -1;
    }
    h->ver_ihl = (u8)((IPV4_VERSION << 4) | IPV4_IHL_MIN);
    h->tos = 0;
    net_put_be16(&h->total_len, (u16)(IPV4_HDR_LEN + payload));
    net_put_be16(&h->id, g_ip_id++);
    net_put_be16(&h->frag_off, IPV4_FLAG_DF);
    h->ttl = ttl;
    h->proto = proto;
    h->checksum = 0;
    h->src = src.be;
    h->dst = dst.be;
    u16 csum = net_checksum(h, IPV4_HDR_LEN);
    h->checksum = csum;
    nb->net = (uint8_t *)h;
    return 0;
}

int ipv4_output(struct netbuf *nb, struct ip_addr src, struct ip_addr dst,
                u8 proto, u8 ttl) {
    struct ip_addr next_hop = dst;
    struct netif *nif = NULL;

    struct ip_route *r = route_lookup(dst, &next_hop);
    if (r != NULL) {
        nif = r->nif;
    } else {
        nif = netif_for_dest(dst);
        next_hop = dst;
    }
    if (nif == NULL) {
        nif = netif_default();
    }
    if (nif == NULL) {
        netbuf_free(nb);
        return -1;
    }

    if (ip_is_zero(src)) {
        src = nif->ip;
    }

    if (ipv4_build(nb, src, dst, proto, ttl) != 0) {
        netbuf_free(nb);
        return -1;
    }

    /* Resolve the next-hop link address. */
    struct mac_addr dmac;
    int rc = arp_resolve(nif, next_hop, &dmac);
    if (rc != 0) {
        /* Pending ARP or error: drop this packet (no queue in the foundation). */
        nif->stats.tx_dropped++;
        netbuf_free(nb);
        return rc > 0 ? 1 : -1;
    }

    if (eth_build(nb, &dmac, &nif->mac, ETH_P_IPV4) != 0) {
        netbuf_free(nb);
        return -1;
    }
    return netif_transmit(nif, nb);
}

void ipv4_input(struct netif *nif, struct netbuf *nb) {
    if (netbuf_len(nb) < IPV4_HDR_LEN) {
        nif->stats.rx_errors++;
        return;
    }
    struct ipv4_header *h = (struct ipv4_header *)nb->data;
    if (ipv4_version(h) != IPV4_VERSION) {
        nif->stats.rx_errors++;
        return;
    }
    u16 hdr_bytes = ipv4_hdr_bytes(h);
    if (hdr_bytes < IPV4_HDR_LEN || netbuf_len(nb) < hdr_bytes) {
        nif->stats.rx_errors++;
        return;
    }
    /* Header checksum: a valid header sums to zero. */
    if (net_checksum(h, hdr_bytes) != 0) {
        nif->stats.rx_errors++;
        return;
    }

    u16 total_len = net_get_be16(&h->total_len);
    if (total_len < hdr_bytes || total_len > netbuf_len(nb)) {
        /* Trust total_len only if it fits; otherwise use what we have. */
        if (total_len >= hdr_bytes && total_len <= netbuf_len(nb)) {
            netbuf_trim(nb, total_len);
        }
    } else {
        netbuf_trim(nb, total_len);
    }

    struct ip_addr src = { h->src };
    struct ip_addr dst = { h->dst };
    u8 proto = h->proto;

    nb->net = nb->data;
    /* Strip the IP header so L4 handlers see their own header at nb->data. */
    netbuf_pull(nb, hdr_bytes);

    switch (proto) {
    case IP_PROTO_ICMP:
        icmp_input(nif, nb, src, dst);
        break;
    case IP_PROTO_UDP:
        udp_input(nif, nb, src, dst);
        break;
    case IP_PROTO_TCP:
        tcp_input(nif, nb, src, dst);
        break;
    default:
        nif->stats.rx_dropped++;
        break;
    }
}
