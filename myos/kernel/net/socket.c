/* Socket table + BSD-ish API implementation. */
#include "socket.h"
#include "udp.h"
#include "tcp.h"
#include "ipv4.h"
#include "net_if.h"
#include "string.h"

static struct socket g_sockets[SOCKET_MAX];

void net_socket_init(void) {
    for (unsigned i = 0; i < SOCKET_MAX; i++) {
        memset(&g_sockets[i], 0, sizeof(g_sockets[i]));
    }
}

struct socket *sock_get(int sd) {
    if (sd < 0 || (unsigned)sd >= SOCKET_MAX || !g_sockets[sd].in_use) {
        return NULL;
    }
    return &g_sockets[sd];
}

int sock_create(int type) {
    if (type != SOCK_DGRAM && type != SOCK_STREAM) {
        return -1;
    }
    for (unsigned i = 0; i < SOCKET_MAX; i++) {
        if (!g_sockets[i].in_use) {
            struct socket *s = &g_sockets[i];
            memset(s, 0, sizeof(*s));
            s->in_use = 1;
            s->type = type;
            if (type == SOCK_STREAM) {
                s->tcb = tcp_tcb_alloc();
                if (s->tcb == NULL) {
                    s->in_use = 0;
                    return -1;
                }
            }
            return (int)i;
        }
    }
    return -1;
}

/* UDP demux callback: queue the datagram into the owning socket. */
static void sock_udp_recv(void *ctx, struct ip_addr src, u16 src_port,
                          struct ip_addr dst, u16 dst_port,
                          const void *data, u16 len) {
    (void)dst; (void)dst_port;
    struct socket *s = (struct socket *)ctx;
    if (s == NULL || !s->in_use || s->type != SOCK_DGRAM) {
        return;
    }
    if (s->rx_count >= SOCKET_RX_QUEUE) {
        return;   /* drop on overflow */
    }
    struct sock_dgram *d = &s->rxq[s->rx_tail];
    d->src = src;
    d->src_port = src_port;
    u16 n = len;
    if (n > SOCKET_RX_PAYLOAD) {
        n = SOCKET_RX_PAYLOAD;
    }
    d->len = n;
    if (n > 0) {
        memcpy(d->data, data, n);
    }
    s->rx_tail = (s->rx_tail + 1) % SOCKET_RX_QUEUE;
    s->rx_count++;
}

int sock_bind(int sd, struct ip_addr ip, u16 port) {
    struct socket *s = sock_get(sd);
    if (s == NULL || s->bound) {
        return -1;
    }
    if (s->type == SOCK_DGRAM) {
        if (port == 0) {
            port = udp_ephemeral_port();
            if (port == 0) {
                return -1;
            }
        }
        if (udp_bind(ip, port, sock_udp_recv, s) != 0) {
            return -1;
        }
    } else { /* SOCK_STREAM */
        if (port == 0) {
            port = udp_ephemeral_port();   /* reuse the ephemeral pool */
        }
        if (s->tcb != NULL) {
            tcp_listen(s->tcb, ip, port);
        }
    }
    s->local_ip = ip;
    s->local_port = port;
    s->bound = 1;
    return 0;
}

int sock_connect(int sd, struct ip_addr ip, u16 port) {
    struct socket *s = sock_get(sd);
    if (s == NULL) {
        return -1;
    }
    if (s->type == SOCK_DGRAM) {
        /* Ensure we have a local binding so replies can be received. */
        if (!s->bound) {
            if (sock_bind(sd, IP_ADDR_ANY, 0) != 0) {
                return -1;
            }
        }
        s->remote_ip = ip;
        s->remote_port = port;
        s->connected = 1;
        return 0;
    }
    /* SOCK_STREAM: open the TCP connection. */
    if (s->tcb == NULL) {
        return -1;
    }
    if (!s->bound) {
        s->local_port = udp_ephemeral_port();
        s->local_ip = IP_ADDR_ANY;
        s->bound = 1;
    }
    s->remote_ip = ip;
    s->remote_port = port;
    s->connected = 1;
    return tcp_connect(s->tcb, s->local_ip, s->local_port, ip, port);
}

int sock_sendto(int sd, const void *buf, u16 len,
                struct ip_addr dst, u16 dport) {
    struct socket *s = sock_get(sd);
    if (s == NULL || s->type != SOCK_DGRAM) {
        return -1;
    }
    if (!s->bound) {
        if (sock_bind(sd, IP_ADDR_ANY, 0) != 0) {
            return -1;
        }
    }
    if (ip_is_zero(dst) && dport == 0 && s->connected) {
        dst = s->remote_ip;
        dport = s->remote_port;
    }
    if (udp_output(s->local_ip, s->local_port, dst, dport, buf, len) < 0) {
        return -1;
    }
    return (int)len;
}

int sock_recvfrom(int sd, void *buf, u16 cap,
                  struct ip_addr *src, u16 *sport) {
    struct socket *s = sock_get(sd);
    if (s == NULL || s->type != SOCK_DGRAM) {
        return -1;
    }
    if (s->rx_count == 0) {
        return -1;
    }
    struct sock_dgram *d = &s->rxq[s->rx_head];
    u16 n = d->len;
    if (n > cap) {
        n = cap;
    }
    if (n > 0) {
        memcpy(buf, d->data, n);
    }
    if (src != NULL) {
        *src = d->src;
    }
    if (sport != NULL) {
        *sport = d->src_port;
    }
    s->rx_head = (s->rx_head + 1) % SOCKET_RX_QUEUE;
    s->rx_count--;
    return (int)n;
}

int sock_close(int sd) {
    struct socket *s = sock_get(sd);
    if (s == NULL) {
        return -1;
    }
    if (s->type == SOCK_DGRAM && s->bound) {
        udp_unbind(s->local_ip, s->local_port);
    }
    if (s->type == SOCK_STREAM && s->tcb != NULL) {
        tcp_close(s->tcb);
        tcp_tcb_free(s->tcb);
        s->tcb = NULL;
    }
    s->in_use = 0;
    return 0;
}
