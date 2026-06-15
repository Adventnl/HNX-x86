/* Address Resolution Protocol (ARP) over Ethernet/IPv4. */
#ifndef MYOS_NET_ARP_H
#define MYOS_NET_ARP_H

#include "types.h"
#include "ethernet.h"
#include "net_addr.h"
#include "net_if.h"
#include "netbuf.h"

/* Hardware / protocol types and opcodes (host order). */
#define ARP_HTYPE_ETHER 1u
#define ARP_PTYPE_IPV4  0x0800u
#define ARP_OP_REQUEST  1u
#define ARP_OP_REPLY    2u

struct arp_header {
    u16 htype;     /* hardware type            */
    u16 ptype;     /* protocol type            */
    u8  hlen;      /* hardware addr length (6) */
    u8  plen;      /* protocol addr length (4) */
    u16 oper;      /* operation                */
    u8  sha[ETH_ALEN];   /* sender hardware addr */
    u8  spa[4];          /* sender protocol addr */
    u8  tha[ETH_ALEN];   /* target hardware addr */
    u8  tpa[4];          /* target protocol addr */
} __attribute__((packed));

#define ARP_HDR_LEN ((u16)sizeof(struct arp_header))

/* Cache entry states. */
enum arp_state {
    ARP_FREE = 0,
    ARP_INCOMPLETE,   /* request sent, awaiting reply */
    ARP_RESOLVED,
};

struct arp_entry {
    enum arp_state  state;
    struct ip_addr  ip;
    struct mac_addr mac;
    u32             age;   /* monotonically incremented bookkeeping */
};

#define ARP_CACHE_SIZE 16u

void arp_init(void);

/* Look up `ip` in the cache. If resolved, copies the MAC into *out and returns
 * 0. If unknown, inserts an INCOMPLETE entry, emits an ARP request on `nif`,
 * and returns 1 (pending). Returns -1 on error. */
int  arp_resolve(struct netif *nif, struct ip_addr ip, struct mac_addr *out);

/* Insert or update a cache entry (used by arp_input and tests). */
void arp_cache_insert(struct ip_addr ip, const struct mac_addr *mac);

/* Look up without triggering a request. Returns 0 + fills *out if resolved. */
int  arp_cache_lookup(struct ip_addr ip, struct mac_addr *out);

/* Emit a broadcast ARP request for `target` on `nif`. */
int  arp_send_request(struct netif *nif, struct ip_addr target);

/* Gratuitous ARP announcing nif->ip/nif->mac (broadcast). */
int  arp_announce(struct netif *nif);

/* Handle a received ARP packet (nb->data at the ARP header, Ethernet already
 * stripped). May reply. Does not free nb. */
void arp_input(struct netif *nif, struct netbuf *nb,
               const struct mac_addr *src);

unsigned arp_cache_count(void);

#endif /* MYOS_NET_ARP_H */
