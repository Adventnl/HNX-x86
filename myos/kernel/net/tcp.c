/* TCP foundation implementation: TCB table, state machine, segment codec. */
#include "tcp.h"
#include "ipv4.h"
#include "checksum.h"
#include "net_endian.h"
#include "string.h"

static struct tcp_tcb g_tcbs[TCP_TCB_MAX];
static u32            g_iss_clock;   /* coarse ISS generator */

void tcp_init(void) {
    for (unsigned i = 0; i < TCP_TCB_MAX; i++) {
        g_tcbs[i].in_use = 0;
        g_tcbs[i].state = TCP_CLOSED;
    }
    g_iss_clock = 0x10000;
}

const char *tcp_state_name(enum tcp_state s) {
    switch (s) {
    case TCP_CLOSED:       return "CLOSED";
    case TCP_LISTEN:       return "LISTEN";
    case TCP_SYN_SENT:     return "SYN_SENT";
    case TCP_SYN_RECEIVED: return "SYN_RECEIVED";
    case TCP_ESTABLISHED:  return "ESTABLISHED";
    case TCP_FIN_WAIT_1:   return "FIN_WAIT_1";
    case TCP_FIN_WAIT_2:   return "FIN_WAIT_2";
    case TCP_CLOSE_WAIT:   return "CLOSE_WAIT";
    case TCP_CLOSING:      return "CLOSING";
    case TCP_LAST_ACK:     return "LAST_ACK";
    case TCP_TIME_WAIT:    return "TIME_WAIT";
    default:               return "?";
    }
}

static u32 tcp_gen_iss(void) {
    /* Monotonic, deterministic generator (no clock dependency for tests). */
    g_iss_clock += 64000;
    return g_iss_clock;
}

struct tcp_tcb *tcp_tcb_alloc(void) {
    for (unsigned i = 0; i < TCP_TCB_MAX; i++) {
        if (!g_tcbs[i].in_use) {
            struct tcp_tcb *t = &g_tcbs[i];
            memset(t, 0, sizeof(*t));
            t->in_use = 1;
            t->state = TCP_CLOSED;
            t->rcv_wnd = 65535;
            t->snd_wnd = 65535;
            return t;
        }
    }
    return NULL;
}

void tcp_tcb_free(struct tcp_tcb *tcb) {
    if (tcb != NULL) {
        tcb->in_use = 0;
        tcb->state = TCP_CLOSED;
    }
}

struct tcp_tcb *tcp_lookup(struct ip_addr local_ip, u16 local_port,
                           struct ip_addr remote_ip, u16 remote_port) {
    struct tcp_tcb *listener = NULL;
    for (unsigned i = 0; i < TCP_TCB_MAX; i++) {
        struct tcp_tcb *t = &g_tcbs[i];
        if (!t->in_use || t->local_port != local_port) {
            continue;
        }
        if (t->state != TCP_LISTEN &&
            t->remote_port == remote_port &&
            ip_equal(t->remote_ip, remote_ip) &&
            (ip_is_zero(t->local_ip) || ip_equal(t->local_ip, local_ip))) {
            return t;
        }
        if (t->state == TCP_LISTEN &&
            (ip_is_zero(t->local_ip) || ip_equal(t->local_ip, local_ip))) {
            listener = t;
        }
    }
    return listener;
}

int tcp_listen(struct tcp_tcb *tcb, struct ip_addr local_ip, u16 local_port) {
    if (tcb == NULL) {
        return -1;
    }
    tcb->local_ip = local_ip;
    tcb->local_port = local_port;
    tcb->remote_ip = IP_ADDR_ANY;
    tcb->remote_port = 0;
    tcb->state = TCP_LISTEN;
    return 0;
}

struct netbuf *tcp_build_segment(struct ip_addr src, u16 src_port,
                                 struct ip_addr dst, u16 dst_port,
                                 u32 seq, u32 ack, u8 flags, u16 window,
                                 const void *data, u16 len) {
    struct netbuf *nb = netbuf_alloc_headroom((size_t)TCP_HDR_LEN + len,
                                              NETBUF_DEFAULT_HEADROOM);
    if (nb == NULL) {
        return NULL;
    }
    struct tcp_header *th =
        (struct tcp_header *)netbuf_put(nb, TCP_HDR_LEN);
    if (th == NULL) {
        netbuf_free(nb);
        return NULL;
    }
    net_put_be16(&th->src_port, src_port);
    net_put_be16(&th->dst_port, dst_port);
    net_put_be32(&th->seq, seq);
    net_put_be32(&th->ack, ack);
    th->data_off = (u8)((TCP_HDR_LEN / 4u) << 4);
    th->flags = flags;
    net_put_be16(&th->window, window);
    th->checksum = 0;
    net_put_be16(&th->urg_ptr, 0);

    if (len > 0) {
        uint8_t *body = netbuf_put(nb, len);
        if (body == NULL) {
            netbuf_free(nb);
            return NULL;
        }
        if (data != NULL) {
            memcpy(body, data, len);
        } else {
            memset(body, 0, len);
        }
    }

    u16 total = (u16)(TCP_HDR_LEN + len);
    th->checksum = net_transport_checksum(src.be, dst.be, IP_PROTO_TCP,
                                          nb->data, total);
    return nb;
}

int tcp_send_segment(struct tcp_tcb *tcb, u8 flags,
                     const void *data, u16 len) {
    if (tcb == NULL) {
        return -1;
    }
    struct netbuf *nb = tcp_build_segment(tcb->local_ip, tcb->local_port,
                                          tcb->remote_ip, tcb->remote_port,
                                          tcb->snd_nxt, tcb->rcv_nxt, flags,
                                          (u16)tcb->rcv_wnd, data, len);
    if (nb == NULL) {
        return -1;
    }
    /* Advance SND.NXT for SYN/FIN (each consumes one sequence number) and for
     * any payload. */
    if (flags & TCP_F_SYN) {
        tcb->snd_nxt += 1;
    }
    tcb->snd_nxt += len;
    if (flags & TCP_F_FIN) {
        tcb->snd_nxt += 1;
    }
    return ipv4_output(nb, tcb->local_ip, tcb->remote_ip, IP_PROTO_TCP, 64);
}

int tcp_connect(struct tcp_tcb *tcb, struct ip_addr local_ip, u16 local_port,
                struct ip_addr remote_ip, u16 remote_port) {
    if (tcb == NULL) {
        return -1;
    }
    tcb->local_ip = local_ip;
    tcb->local_port = local_port;
    tcb->remote_ip = remote_ip;
    tcb->remote_port = remote_port;
    tcb->iss = tcp_gen_iss();
    tcb->snd_una = tcb->iss;
    tcb->snd_nxt = tcb->iss;     /* tcp_send_segment bumps past the SYN */
    tcb->rcv_nxt = 0;
    tcb->state = TCP_SYN_SENT;
    return tcp_send_segment(tcb, TCP_F_SYN, NULL, 0);
}

int tcp_close(struct tcp_tcb *tcb) {
    if (tcb == NULL) {
        return -1;
    }
    switch (tcb->state) {
    case TCP_ESTABLISHED:
        tcb->state = TCP_FIN_WAIT_1;
        return tcp_send_segment(tcb, TCP_F_FIN | TCP_F_ACK, NULL, 0);
    case TCP_CLOSE_WAIT:
        tcb->state = TCP_LAST_ACK;
        return tcp_send_segment(tcb, TCP_F_FIN | TCP_F_ACK, NULL, 0);
    case TCP_SYN_SENT:
    case TCP_SYN_RECEIVED:
    case TCP_LISTEN:
        tcb->state = TCP_CLOSED;
        return 0;
    default:
        return 0;
    }
}

/* Drive the connection state machine for one inbound segment. This implements
 * the handshake and close transitions (the data-transfer foundation). */
static void tcp_state_machine(struct tcp_tcb *tcb, const struct tcp_header *th,
                              u32 seq, u32 ack, u8 flags, u16 seglen) {
    (void)ack;
    switch (tcb->state) {
    case TCP_LISTEN:
        if (flags & TCP_F_SYN) {
            /* Passive open: peer 4-tuple was recorded by tcp_input; send
             * SYN+ACK and move to SYN_RECEIVED. */
            (void)th;
            tcb->irs = seq;
            tcb->rcv_nxt = seq + 1;
            tcb->iss = tcp_gen_iss();
            tcb->snd_una = tcb->iss;
            tcb->snd_nxt = tcb->iss;
            tcb->state = TCP_SYN_RECEIVED;
            tcp_send_segment(tcb, TCP_F_SYN | TCP_F_ACK, NULL, 0);
        }
        break;

    case TCP_SYN_SENT:
        if ((flags & (TCP_F_SYN | TCP_F_ACK)) == (TCP_F_SYN | TCP_F_ACK)) {
            tcb->irs = seq;
            tcb->rcv_nxt = seq + 1;
            tcb->snd_una = ack;
            tcb->state = TCP_ESTABLISHED;
            tcp_send_segment(tcb, TCP_F_ACK, NULL, 0);
        } else if (flags & TCP_F_SYN) {
            /* Simultaneous open. */
            tcb->irs = seq;
            tcb->rcv_nxt = seq + 1;
            tcb->state = TCP_SYN_RECEIVED;
            tcp_send_segment(tcb, TCP_F_SYN | TCP_F_ACK, NULL, 0);
        }
        break;

    case TCP_SYN_RECEIVED:
        if (flags & TCP_F_ACK) {
            tcb->snd_una = ack;
            tcb->state = TCP_ESTABLISHED;
        }
        break;

    case TCP_ESTABLISHED:
        if (seglen > 0) {
            tcb->rcv_nxt = seq + seglen;
            tcp_send_segment(tcb, TCP_F_ACK, NULL, 0);
        }
        if (flags & TCP_F_FIN) {
            tcb->rcv_nxt = seq + seglen + 1;
            tcb->state = TCP_CLOSE_WAIT;
            tcp_send_segment(tcb, TCP_F_ACK, NULL, 0);
        }
        break;

    case TCP_FIN_WAIT_1:
        if (flags & TCP_F_ACK) {
            tcb->state = TCP_FIN_WAIT_2;
        }
        if (flags & TCP_F_FIN) {
            tcb->rcv_nxt = seq + 1;
            tcb->state = (tcb->state == TCP_FIN_WAIT_2) ? TCP_TIME_WAIT
                                                        : TCP_CLOSING;
            tcp_send_segment(tcb, TCP_F_ACK, NULL, 0);
        }
        break;

    case TCP_FIN_WAIT_2:
        if (flags & TCP_F_FIN) {
            tcb->rcv_nxt = seq + 1;
            tcb->state = TCP_TIME_WAIT;
            tcp_send_segment(tcb, TCP_F_ACK, NULL, 0);
        }
        break;

    case TCP_CLOSING:
        if (flags & TCP_F_ACK) {
            tcb->state = TCP_TIME_WAIT;
        }
        break;

    case TCP_LAST_ACK:
        if (flags & TCP_F_ACK) {
            tcb->state = TCP_CLOSED;
            tcp_tcb_free(tcb);
        }
        break;

    case TCP_TIME_WAIT:
    case TCP_CLOSE_WAIT:
    case TCP_CLOSED:
    default:
        break;
    }
}

void tcp_input(struct netif *nif, struct netbuf *nb,
               struct ip_addr src, struct ip_addr dst) {
    (void)nif;
    if (netbuf_len(nb) < TCP_HDR_LEN) {
        return;
    }
    const struct tcp_header *th = (const struct tcp_header *)nb->data;
    u16 hdr_bytes = tcp_header_bytes(th);
    if (hdr_bytes < TCP_HDR_LEN || hdr_bytes > netbuf_len(nb)) {
        return;
    }

    /* Verify the checksum over the pseudo-header + segment. */
    u16 seg_total = (u16)netbuf_len(nb);
    if (net_transport_checksum(src.be, dst.be, IP_PROTO_TCP,
                               nb->data, seg_total) != 0) {
        return;
    }

    u16 src_port = net_get_be16(&th->src_port);
    u16 dst_port = net_get_be16(&th->dst_port);
    u32 seq = net_get_be32(&th->seq);
    u32 ack = net_get_be32(&th->ack);
    u8  flags = th->flags;
    u16 seglen = (u16)(seg_total - hdr_bytes);

    struct tcp_tcb *tcb = tcp_lookup(dst, dst_port, src, src_port);
    if (tcb == NULL) {
        return;   /* no socket; a full stack would send RST */
    }

    /* Bind a freshly-accepting LISTEN TCB to the peer 4-tuple. */
    if (tcb->state == TCP_LISTEN) {
        tcb->remote_ip = src;
        tcb->remote_port = src_port;
        tcb->local_ip = dst;
    }

    tcp_state_machine(tcb, th, seq, ack, flags, seglen);
}
