/* UDP: header, bind table, input demux, output with pseudo-header checksum. */
#ifndef MYOS_NET_UDP_H
#define MYOS_NET_UDP_H

#include "types.h"
#include "net_addr.h"
#include "net_if.h"
#include "netbuf.h"

struct udp_header {
    u16 src_port;   /* big-endian */
    u16 dst_port;   /* big-endian */
    u16 length;     /* big-endian: header + payload */
    u16 checksum;   /* big-endian (0 = unused) */
} __attribute__((packed));

#define UDP_HDR_LEN ((u16)sizeof(struct udp_header))

/* Receive callback for a UDP binding. `data`/`len` are the datagram payload;
 * src/dst are the IPv4 endpoints and ports are host order. `ctx` is the value
 * passed at bind time. */
typedef void (*udp_recv_fn)(void *ctx, struct ip_addr src, u16 src_port,
                            struct ip_addr dst, u16 dst_port,
                            const void *data, u16 len);

struct udp_binding {
    int          in_use;
    struct ip_addr local_ip;   /* 0 => any */
    u16          local_port;
    udp_recv_fn  recv;
    void        *ctx;
};

#define UDP_BIND_MAX 16u

void udp_init(void);

/* Register a receiver for (local_ip, local_port). local_ip may be IP_ADDR_ANY.
 * Returns 0 on success, -1 if the port is taken or the table is full. */
int  udp_bind(struct ip_addr local_ip, u16 local_port,
              udp_recv_fn recv, void *ctx);
void udp_unbind(struct ip_addr local_ip, u16 local_port);

/* Allocate an ephemeral local port not currently bound. */
u16  udp_ephemeral_port(void);

/* Send `len` payload bytes to (dst, dst_port) from (src, src_port). src may be
 * IP_ADDR_ANY (filled from the egress interface). Builds UDP header + checksum
 * then hands to ipv4_output. Returns ipv4_output's result. */
int  udp_output(struct ip_addr src, u16 src_port,
                struct ip_addr dst, u16 dst_port,
                const void *data, u16 len);

/* Inbound demux (called by ipv4_input; nb->data at the UDP header). */
void udp_input(struct netif *nif, struct netbuf *nb,
               struct ip_addr src, struct ip_addr dst);

u64 udp_datagrams_received(void);

#endif /* MYOS_NET_UDP_H */
