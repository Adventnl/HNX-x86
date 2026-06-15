/* Ethernet II framing. */
#ifndef MYOS_NET_ETHERNET_H
#define MYOS_NET_ETHERNET_H

#include "types.h"
#include "netbuf.h"

#define ETH_ALEN     6u      /* MAC address length            */
#define ETH_HLEN     14u     /* Ethernet II header length     */
#define ETH_MIN_LEN  60u     /* minimum frame (excl. FCS)     */
#define ETH_MAX_DATA 1500u   /* default MTU                   */

/* EtherTypes (host order; stored big-endian on the wire). */
#define ETH_P_IPV4   0x0800u
#define ETH_P_ARP    0x0806u
#define ETH_P_IPV6   0x86DDu

struct mac_addr {
    u8 addr[ETH_ALEN];
};

struct eth_header {
    u8  dst[ETH_ALEN];
    u8  src[ETH_ALEN];
    u16 ethertype;        /* big-endian on the wire */
} __attribute__((packed));

extern const struct mac_addr eth_broadcast;
extern const struct mac_addr eth_zero;

static inline int mac_equal(const struct mac_addr *a, const struct mac_addr *b) {
    for (unsigned i = 0; i < ETH_ALEN; i++) {
        if (a->addr[i] != b->addr[i]) {
            return 0;
        }
    }
    return 1;
}

int mac_is_broadcast(const struct mac_addr *m);
int mac_is_multicast(const struct mac_addr *m);
int mac_is_zero(const struct mac_addr *m);

void mac_copy(struct mac_addr *dst, const struct mac_addr *src);
/* Build a mac_addr from six octets. */
struct mac_addr mac_make(u8 a, u8 b, u8 c, u8 d, u8 e, u8 f);

/* Build an Ethernet header in front of the current netbuf data (push). Returns
 * 0 on success, -1 on no headroom. ethertype is in host order. */
int eth_build(struct netbuf *nb, const struct mac_addr *dst,
              const struct mac_addr *src, u16 ethertype);

/* Parse the Ethernet header from the front of nb. On success fills the src/dst
 * (if non-NULL) and ethertype (host order) outputs, stamps nb->eth, advances data
 * past the header (pull) and returns 0. Returns -1 if the frame is too short. */
int eth_parse(struct netbuf *nb, struct mac_addr *dst, struct mac_addr *src,
              u16 *ethertype);

#endif /* MYOS_NET_ETHERNET_H */
