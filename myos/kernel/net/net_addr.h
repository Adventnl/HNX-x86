/* IPv4 address type, shared across the network stack to avoid header cycles. */
#ifndef MYOS_NET_ADDR_H
#define MYOS_NET_ADDR_H

#include "types.h"
#include "net_endian.h"

/* An IPv4 address stored in network byte order (big-endian) for direct use in
 * packet headers and checksums. */
struct ip_addr {
    u32 be;   /* big-endian 32-bit address */
};

/* Build from four dotted-decimal octets (a.b.c.d). */
static inline struct ip_addr ip_make(u8 a, u8 b, u8 c, u8 d) {
    struct ip_addr ip;
    ip.be = ((u32)a) | ((u32)b << 8) | ((u32)c << 16) | ((u32)d << 24);
    return ip;
}

/* Build from a host-order 32-bit value. */
static inline struct ip_addr ip_from_host(u32 host) {
    struct ip_addr ip;
    ip.be = net_htonl(host);
    return ip;
}

static inline u32 ip_to_host(struct ip_addr ip) {
    return net_ntohl(ip.be);
}

static inline int ip_equal(struct ip_addr a, struct ip_addr b) {
    return a.be == b.be;
}

static inline int ip_is_zero(struct ip_addr a) {
    return a.be == 0;
}

#define IP_ADDR_ANY       (ip_from_host(0x00000000u))
#define IP_ADDR_BROADCAST (ip_from_host(0xFFFFFFFFu))
#define IP_ADDR_LOOPBACK  (ip_from_host(0x7F000001u))

static inline int ip_is_broadcast(struct ip_addr a) {
    return a.be == 0xFFFFFFFFu;
}

/* Is `a` in the same subnet as `net` given `mask` (all network order)? */
static inline int ip_same_subnet(struct ip_addr a, struct ip_addr net,
                                 struct ip_addr mask) {
    return (a.be & mask.be) == (net.be & mask.be);
}

#endif /* MYOS_NET_ADDR_H */
