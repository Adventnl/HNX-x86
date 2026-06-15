/* ARP cache + request/reply state machine. */
#include "arp.h"
#include "net_endian.h"
#include "string.h"
#include "log.h"

static struct arp_entry g_arp_cache[ARP_CACHE_SIZE];
static u32              g_arp_clock;

void arp_init(void) {
    for (unsigned i = 0; i < ARP_CACHE_SIZE; i++) {
        g_arp_cache[i].state = ARP_FREE;
        g_arp_cache[i].ip = IP_ADDR_ANY;
        g_arp_cache[i].mac = eth_zero;
        g_arp_cache[i].age = 0;
    }
    g_arp_clock = 0;
}

static struct arp_entry *arp_find(struct ip_addr ip) {
    for (unsigned i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].state != ARP_FREE &&
            ip_equal(g_arp_cache[i].ip, ip)) {
            return &g_arp_cache[i];
        }
    }
    return NULL;
}

/* Pick a slot for a new entry: free slot, else the oldest. */
static struct arp_entry *arp_alloc_slot(void) {
    struct arp_entry *oldest = &g_arp_cache[0];
    for (unsigned i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].state == ARP_FREE) {
            return &g_arp_cache[i];
        }
        if (g_arp_cache[i].age < oldest->age) {
            oldest = &g_arp_cache[i];
        }
    }
    return oldest;
}

void arp_cache_insert(struct ip_addr ip, const struct mac_addr *mac) {
    if (ip_is_zero(ip)) {
        return;
    }
    struct arp_entry *e = arp_find(ip);
    if (e == NULL) {
        e = arp_alloc_slot();
        e->ip = ip;
    }
    e->state = ARP_RESOLVED;
    mac_copy(&e->mac, mac);
    e->age = ++g_arp_clock;
}

int arp_cache_lookup(struct ip_addr ip, struct mac_addr *out) {
    struct arp_entry *e = arp_find(ip);
    if (e != NULL && e->state == ARP_RESOLVED) {
        if (out != NULL) {
            mac_copy(out, &e->mac);
        }
        return 0;
    }
    return -1;
}

unsigned arp_cache_count(void) {
    unsigned n = 0;
    for (unsigned i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].state != ARP_FREE) {
            n++;
        }
    }
    return n;
}

/* Build an ARP packet of the given opcode into a fresh netbuf and frame it. */
static struct netbuf *arp_build(struct netif *nif, u16 oper,
                                struct ip_addr tpa,
                                const struct mac_addr *tha,
                                const struct mac_addr *eth_dst) {
    struct netbuf *nb = netbuf_alloc(ARP_HDR_LEN + ETH_HLEN);
    if (nb == NULL) {
        return NULL;
    }
    netbuf_reserve(nb, ETH_HLEN);
    struct arp_header *ah = (struct arp_header *)netbuf_put(nb, ARP_HDR_LEN);
    if (ah == NULL) {
        netbuf_free(nb);
        return NULL;
    }
    net_put_be16(&ah->htype, ARP_HTYPE_ETHER);
    net_put_be16(&ah->ptype, ARP_PTYPE_IPV4);
    ah->hlen = ETH_ALEN;
    ah->plen = 4;
    net_put_be16(&ah->oper, oper);
    memcpy(ah->sha, nif->mac.addr, ETH_ALEN);
    net_put_be32(ah->spa, ip_to_host(nif->ip));
    memcpy(ah->tha, tha->addr, ETH_ALEN);
    net_put_be32(ah->tpa, ip_to_host(tpa));

    if (eth_build(nb, eth_dst, &nif->mac, ETH_P_ARP) != 0) {
        netbuf_free(nb);
        return NULL;
    }
    return nb;
}

int arp_send_request(struct netif *nif, struct ip_addr target) {
    struct netbuf *nb = arp_build(nif, ARP_OP_REQUEST, target,
                                  &eth_zero, &eth_broadcast);
    if (nb == NULL) {
        return -1;
    }
    return netif_transmit(nif, nb);
}

int arp_announce(struct netif *nif) {
    /* Gratuitous ARP: target protocol address == our own. */
    struct netbuf *nb = arp_build(nif, ARP_OP_REQUEST, nif->ip,
                                  &eth_broadcast, &eth_broadcast);
    if (nb == NULL) {
        return -1;
    }
    return netif_transmit(nif, nb);
}

int arp_resolve(struct netif *nif, struct ip_addr ip, struct mac_addr *out) {
    if (nif == NULL) {
        return -1;
    }
    /* Loopback / self / broadcast resolve trivially. */
    if (nif->flags & NETIF_FLAG_LOOPBACK) {
        if (out != NULL) {
            mac_copy(out, &nif->mac);
        }
        return 0;
    }
    /* Limited broadcast, or the directed broadcast of our own subnet
     * (host bits all ones), resolves to the Ethernet broadcast address. */
    int directed_bcast = (nif->netmask.be != 0) &&
                         ip_same_subnet(ip, nif->ip, nif->netmask) &&
                         ((ip.be & ~nif->netmask.be) == (~nif->netmask.be));
    if (ip_is_broadcast(ip) || directed_bcast) {
        if (out != NULL) {
            mac_copy(out, &eth_broadcast);
        }
        return 0;
    }

    if (arp_cache_lookup(ip, out) == 0) {
        return 0;
    }

    /* Insert an INCOMPLETE entry and fire a request. */
    struct arp_entry *e = arp_find(ip);
    if (e == NULL) {
        e = arp_alloc_slot();
        e->ip = ip;
    }
    e->state = ARP_INCOMPLETE;
    e->age = ++g_arp_clock;
    arp_send_request(nif, ip);
    return 1;   /* pending */
}

void arp_input(struct netif *nif, struct netbuf *nb,
               const struct mac_addr *src) {
    (void)src;
    if (netbuf_len(nb) < ARP_HDR_LEN) {
        return;
    }
    const struct arp_header *ah = (const struct arp_header *)nb->data;
    if (net_get_be16(&ah->htype) != ARP_HTYPE_ETHER ||
        net_get_be16(&ah->ptype) != ARP_PTYPE_IPV4 ||
        ah->hlen != ETH_ALEN || ah->plen != 4) {
        return;
    }
    u16 oper = net_get_be16(&ah->oper);
    struct ip_addr spa = ip_from_host(net_get_be32(ah->spa));
    struct ip_addr tpa = ip_from_host(net_get_be32(ah->tpa));

    struct mac_addr sha;
    memcpy(sha.addr, ah->sha, ETH_ALEN);

    /* Learn the sender mapping regardless of opcode. */
    if (!ip_is_zero(spa)) {
        arp_cache_insert(spa, &sha);
    }

    /* If a request targets our address, reply. */
    if (oper == ARP_OP_REQUEST && !ip_is_zero(nif->ip) &&
        ip_equal(tpa, nif->ip)) {
        struct netbuf *reply = arp_build(nif, ARP_OP_REPLY, spa, &sha, &sha);
        if (reply != NULL) {
            netif_transmit(nif, reply);
        }
    }
}
