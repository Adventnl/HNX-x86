/* ICMP (echo request/reply = ping). */
#ifndef MYOS_NET_ICMP_H
#define MYOS_NET_ICMP_H

#include "types.h"
#include "net_addr.h"
#include "net_if.h"
#include "netbuf.h"

/* ICMP types. */
#define ICMP_ECHO_REPLY   0u
#define ICMP_DEST_UNREACH 3u
#define ICMP_ECHO_REQUEST 8u
#define ICMP_TIME_EXCEED  11u

struct icmp_header {
    u8  type;
    u8  code;
    u16 checksum;   /* big-endian, over the whole ICMP message */
    u16 id;         /* big-endian (echo) */
    u16 seq;        /* big-endian (echo) */
} __attribute__((packed));

#define ICMP_HDR_LEN ((u16)sizeof(struct icmp_header))

void icmp_init(void);

/* Handle an inbound ICMP message (nb->data at the ICMP header). Replies to echo
 * requests via ipv4_output. */
void icmp_input(struct netif *nif, struct netbuf *nb,
                struct ip_addr src, struct ip_addr dst);

/* Build + send an ICMP echo request to `dst` carrying `len` payload bytes from
 * `payload` (may be NULL for zero-fill). Returns ipv4_output's result. */
int  icmp_send_echo(struct ip_addr dst, u16 id, u16 seq,
                    const void *payload, u16 len);

/* Statistics for the test harness. */
u64 icmp_echo_requests_seen(void);
u64 icmp_echo_replies_sent(void);
u64 icmp_echo_replies_seen(void);

#endif /* MYOS_NET_ICMP_H */
