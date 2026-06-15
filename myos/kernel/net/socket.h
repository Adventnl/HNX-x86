/* Socket table + BSD-ish API over UDP (and TCP foundation).
 *
 * Sockets are referenced by a small integer descriptor into a kernel-global
 * table (future per-process descriptor tables can wrap these). Datagram sockets
 * receive into an in-socket queue filled by the UDP demux callback; recvfrom
 * drains it. Stream sockets are wired to a TCB (foundation: connect/close drive
 * the state machine; data queueing is the documented next step).
 */
#ifndef MYOS_NET_SOCKET_H
#define MYOS_NET_SOCKET_H

#include "types.h"
#include "net_addr.h"
#include "tcp.h"

/* Address family / socket types (subset). */
#define SOCK_DGRAM  1   /* UDP    */
#define SOCK_STREAM 2   /* TCP    */

#define SOCKET_MAX        16u
#define SOCKET_RX_QUEUE   8u
#define SOCKET_RX_PAYLOAD 1472u   /* fits in a single 1500-MTU UDP datagram */

struct sock_dgram {
    struct ip_addr src;
    u16            src_port;
    u16            len;
    u8             data[SOCKET_RX_PAYLOAD];
};

struct socket {
    int             in_use;
    int             type;          /* SOCK_DGRAM / SOCK_STREAM */
    struct ip_addr  local_ip;
    u16             local_port;
    struct ip_addr  remote_ip;     /* for connected sockets */
    u16             remote_port;
    int             bound;
    int             connected;

    /* Datagram receive ring. */
    struct sock_dgram rxq[SOCKET_RX_QUEUE];
    unsigned          rx_head;
    unsigned          rx_tail;
    unsigned          rx_count;

    /* Stream backing. */
    struct tcp_tcb   *tcb;
};

void net_socket_init(void);

/* Create a socket; returns a descriptor >= 0 or -1. */
int  sock_create(int type);

/* Bind to (ip, port). port 0 picks an ephemeral port for datagram sockets. */
int  sock_bind(int sd, struct ip_addr ip, u16 port);

/* Connect: for datagram sockets just records the peer; for stream sockets opens
 * the TCP connection (SYN). */
int  sock_connect(int sd, struct ip_addr ip, u16 port);

/* Send a datagram to (dst, dport). For connected datagram sockets, dst may be
 * IP_ADDR_ANY/0 to use the connected peer. */
int  sock_sendto(int sd, const void *buf, u16 len,
                 struct ip_addr dst, u16 dport);

/* Receive a datagram. On success returns the byte count and fills the src/sport
 * outputs (if non-NULL). Returns -1 if no datagram is queued. */
int  sock_recvfrom(int sd, void *buf, u16 cap,
                   struct ip_addr *src, u16 *sport);

int  sock_close(int sd);

/* Accessor for tests. */
struct socket *sock_get(int sd);

#endif /* MYOS_NET_SOCKET_H */
