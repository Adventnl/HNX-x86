/* TCP foundation: header, TCB + state machine, segment build/parse, checksum.
 *
 * This implements the structures, checksum, segment encode/decode and the
 * connection state machine transitions. Bulk data transfer / retransmission /
 * windowing are intentionally left as a documented foundation: the TCB tracks
 * send/recv sequence state and the handshake transitions are driven by
 * tcp_input(), which is exercised over loopback in the tests.
 */
#ifndef MYOS_NET_TCP_H
#define MYOS_NET_TCP_H

#include "types.h"
#include "net_addr.h"
#include "net_if.h"
#include "netbuf.h"

struct tcp_header {
    u16 src_port;    /* big-endian */
    u16 dst_port;    /* big-endian */
    u32 seq;         /* big-endian */
    u32 ack;         /* big-endian */
    u8  data_off;    /* high 4 bits = header length in 32-bit words */
    u8  flags;       /* control bits (see TCP_F_*) */
    u16 window;      /* big-endian */
    u16 checksum;    /* big-endian */
    u16 urg_ptr;     /* big-endian */
} __attribute__((packed));

#define TCP_HDR_LEN ((u16)sizeof(struct tcp_header))   /* 20, no options */

/* Control flags. */
#define TCP_F_FIN 0x01u
#define TCP_F_SYN 0x02u
#define TCP_F_RST 0x04u
#define TCP_F_PSH 0x08u
#define TCP_F_ACK 0x10u
#define TCP_F_URG 0x20u

static inline u8 tcp_data_offset_words(const struct tcp_header *h) {
    return (u8)(h->data_off >> 4);
}
static inline u16 tcp_header_bytes(const struct tcp_header *h) {
    return (u16)(tcp_data_offset_words(h) * 4u);
}

/* Connection states (RFC 793). */
enum tcp_state {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT,
};

const char *tcp_state_name(enum tcp_state s);

/* Transmission control block. */
struct tcp_tcb {
    int            in_use;
    enum tcp_state state;

    struct ip_addr local_ip;
    struct ip_addr remote_ip;
    u16            local_port;
    u16            remote_port;

    /* Send sequence space (RFC 793 SND.*). */
    u32 snd_una;   /* oldest unacknowledged */
    u32 snd_nxt;   /* next to send          */
    u32 snd_wnd;   /* send window           */
    u32 iss;       /* initial send sequence */

    /* Receive sequence space (RFC 793 RCV.*). */
    u32 rcv_nxt;   /* next expected         */
    u32 rcv_wnd;   /* receive window        */
    u32 irs;       /* initial recv sequence */
};

#define TCP_TCB_MAX 8u

void tcp_init(void);

/* Allocate / free a TCB. */
struct tcp_tcb *tcp_tcb_alloc(void);
void            tcp_tcb_free(struct tcp_tcb *tcb);

/* Passive open: move a fresh TCB to LISTEN on (local_ip, local_port). */
int  tcp_listen(struct tcp_tcb *tcb, struct ip_addr local_ip, u16 local_port);

/* Active open: pick an ISS, send a SYN, move to SYN_SENT. */
int  tcp_connect(struct tcp_tcb *tcb, struct ip_addr local_ip, u16 local_port,
                 struct ip_addr remote_ip, u16 remote_port);

/* Begin an active close (ESTABLISHED -> FIN_WAIT_1, etc.). */
int  tcp_close(struct tcp_tcb *tcb);

/* Build a bare TCP segment (header + optional payload) into a fresh netbuf and
 * compute the checksum over the IPv4 pseudo-header. Leaves nb->data at the TCP
 * header with IP/Eth headroom reserved. Returns NULL on failure. */
struct netbuf *tcp_build_segment(struct ip_addr src, u16 src_port,
                                 struct ip_addr dst, u16 dst_port,
                                 u32 seq, u32 ack, u8 flags, u16 window,
                                 const void *data, u16 len);

/* Send a control/segment via ipv4_output. */
int  tcp_send_segment(struct tcp_tcb *tcb, u8 flags,
                      const void *data, u16 len);

/* Inbound segment handler (called by ipv4_input; nb->data at the TCP header).
 * Drives the state machine for the matching TCB. */
void tcp_input(struct netif *nif, struct netbuf *nb,
               struct ip_addr src, struct ip_addr dst);

/* Find the TCB matching a 4-tuple (or a LISTEN on the local port). */
struct tcp_tcb *tcp_lookup(struct ip_addr local_ip, u16 local_port,
                           struct ip_addr remote_ip, u16 remote_port);

#endif /* MYOS_NET_TCP_H */
