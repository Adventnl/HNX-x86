/* IPv4: header, parse/build, checksum, routing table, input/output. */
#ifndef MYOS_NET_IPV4_H
#define MYOS_NET_IPV4_H

#include "types.h"
#include "net_addr.h"
#include "net_if.h"
#include "netbuf.h"

/* IP protocol numbers. */
#define IP_PROTO_ICMP 1u
#define IP_PROTO_TCP  6u
#define IP_PROTO_UDP  17u

#define IPV4_VERSION  4u
#define IPV4_IHL_MIN  5u                 /* 5 * 4 = 20 bytes, no options */
#define IPV4_HDR_LEN  20u

struct ipv4_header {
    u8  ver_ihl;     /* version(4) | ihl(4)            */
    u8  tos;         /* type of service / DSCP+ECN     */
    u16 total_len;   /* big-endian: header + payload   */
    u16 id;          /* big-endian identification      */
    u16 frag_off;    /* big-endian: flags(3)|offset(13)*/
    u8  ttl;
    u8  proto;
    u16 checksum;    /* big-endian header checksum     */
    u32 src;         /* big-endian source address      */
    u32 dst;         /* big-endian destination address */
} __attribute__((packed));

#define IPV4_FLAG_DF 0x4000u   /* don't fragment (host order in frag_off) */
#define IPV4_FLAG_MF 0x2000u   /* more fragments                         */

static inline u8 ipv4_version(const struct ipv4_header *h) {
    return (u8)(h->ver_ihl >> 4);
}
static inline u8 ipv4_ihl(const struct ipv4_header *h) {
    return (u8)(h->ver_ihl & 0x0F);
}
static inline u16 ipv4_hdr_bytes(const struct ipv4_header *h) {
    return (u16)(ipv4_ihl(h) * 4u);
}

/* ---- Routing table -------------------------------------------------------- */
#define IP_ROUTE_MAX 8u

struct ip_route {
    struct ip_addr dest;       /* network                */
    struct ip_addr netmask;    /* 0 => default route     */
    struct ip_addr gateway;    /* 0 => on-link           */
    struct netif  *nif;
    int            valid;
    u32            prefix_len;  /* for longest-prefix ordering */
};

void route_init(void);
int  route_add(struct ip_addr dest, struct ip_addr netmask,
               struct ip_addr gateway, struct netif *nif);
/* Longest-prefix-match lookup. On success returns the route and sets *next_hop
 * to the gateway (or `dst` itself if on-link). Returns NULL if unreachable. */
struct ip_route *route_lookup(struct ip_addr dst, struct ip_addr *next_hop);
unsigned route_count(void);

/* ---- I/O ------------------------------------------------------------------ */
void ipv4_init(void);

/* Build an IPv4 header in front of the current netbuf payload (push) and
 * checksum it. proto is the IP protocol; src/dst are network order. nb->data
 * must already point at the L4 payload. Returns 0 on success. */
int  ipv4_build(struct netbuf *nb, struct ip_addr src, struct ip_addr dst,
                u8 proto, u8 ttl);

/* Send an L4 payload (nb->data at the payload start) to `dst` with `proto`.
 * Looks up the route, resolves the next hop via ARP, frames Ethernet, and
 * transmits. Consumes nb on success or failure. Returns 0 on success, 1 if the
 * packet was queued/dropped pending ARP, -1 on hard error. */
int  ipv4_output(struct netbuf *nb, struct ip_addr src, struct ip_addr dst,
                 u8 proto, u8 ttl);

/* Receive an IPv4 packet (Ethernet header already stripped; nb->data at the IP
 * header). Verifies the checksum and dispatches to ICMP/UDP/TCP. Does not free
 * nb (caller owns it). */
void ipv4_input(struct netif *nif, struct netbuf *nb);

/* Per-protocol upper-layer handlers, invoked by ipv4_input with nb->data at the
 * L4 header and the IP addresses passed through. */
void icmp_input(struct netif *nif, struct netbuf *nb,
                struct ip_addr src, struct ip_addr dst);
void udp_input(struct netif *nif, struct netbuf *nb,
               struct ip_addr src, struct ip_addr dst);
void tcp_input(struct netif *nif, struct netbuf *nb,
               struct ip_addr src, struct ip_addr dst);

#endif /* MYOS_NET_IPV4_H */
