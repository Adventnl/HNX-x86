/* UDP implementation. */
#include "udp.h"
#include "ipv4.h"
#include "checksum.h"
#include "net_endian.h"
#include "string.h"

static struct udp_binding g_bindings[UDP_BIND_MAX];
static u16                g_ephemeral_next;
static u64                g_datagrams_received;

void udp_init(void) {
    for (unsigned i = 0; i < UDP_BIND_MAX; i++) {
        g_bindings[i].in_use = 0;
    }
    g_ephemeral_next = 49152;   /* IANA dynamic range start */
    g_datagrams_received = 0;
}

u64 udp_datagrams_received(void) { return g_datagrams_received; }

static struct udp_binding *udp_find(struct ip_addr ip, u16 port) {
    struct udp_binding *wild = NULL;
    for (unsigned i = 0; i < UDP_BIND_MAX; i++) {
        struct udp_binding *b = &g_bindings[i];
        if (!b->in_use || b->local_port != port) {
            continue;
        }
        if (ip_equal(b->local_ip, ip)) {
            return b;
        }
        if (ip_is_zero(b->local_ip)) {
            wild = b;
        }
    }
    return wild;
}

int udp_bind(struct ip_addr local_ip, u16 local_port,
             udp_recv_fn recv, void *ctx) {
    /* Reject an exact duplicate (same ip + port). */
    for (unsigned i = 0; i < UDP_BIND_MAX; i++) {
        struct udp_binding *b = &g_bindings[i];
        if (b->in_use && b->local_port == local_port &&
            ip_equal(b->local_ip, local_ip)) {
            return -1;
        }
    }
    for (unsigned i = 0; i < UDP_BIND_MAX; i++) {
        struct udp_binding *b = &g_bindings[i];
        if (!b->in_use) {
            b->in_use = 1;
            b->local_ip = local_ip;
            b->local_port = local_port;
            b->recv = recv;
            b->ctx = ctx;
            return 0;
        }
    }
    return -1;
}

void udp_unbind(struct ip_addr local_ip, u16 local_port) {
    for (unsigned i = 0; i < UDP_BIND_MAX; i++) {
        struct udp_binding *b = &g_bindings[i];
        if (b->in_use && b->local_port == local_port &&
            ip_equal(b->local_ip, local_ip)) {
            b->in_use = 0;
            return;
        }
    }
}

u16 udp_ephemeral_port(void) {
    for (unsigned tries = 0; tries < 16384; tries++) {
        u16 port = g_ephemeral_next++;
        if (g_ephemeral_next == 0 || g_ephemeral_next < 49152) {
            g_ephemeral_next = 49152;
        }
        int taken = 0;
        for (unsigned i = 0; i < UDP_BIND_MAX; i++) {
            if (g_bindings[i].in_use && g_bindings[i].local_port == port) {
                taken = 1;
                break;
            }
        }
        if (!taken) {
            return port;
        }
    }
    return 0;
}

int udp_output(struct ip_addr src, u16 src_port,
               struct ip_addr dst, u16 dst_port,
               const void *data, u16 len) {
    struct netbuf *nb = netbuf_alloc_headroom((size_t)UDP_HDR_LEN + len,
                                              NETBUF_DEFAULT_HEADROOM);
    if (nb == NULL) {
        return -1;
    }
    struct udp_header *uh =
        (struct udp_header *)netbuf_put(nb, UDP_HDR_LEN);
    if (uh == NULL) {
        netbuf_free(nb);
        return -1;
    }
    net_put_be16(&uh->src_port, src_port);
    net_put_be16(&uh->dst_port, dst_port);
    net_put_be16(&uh->length, (u16)(UDP_HDR_LEN + len));
    uh->checksum = 0;
    if (len > 0) {
        uint8_t *body = netbuf_put(nb, len);
        if (body == NULL) {
            netbuf_free(nb);
            return -1;
        }
        if (data != NULL) {
            memcpy(body, data, len);
        } else {
            memset(body, 0, len);
        }
    }

    /* Pseudo-header checksum. If src is unspecified we still need a source for
     * the checksum; resolve a route to find the egress interface address. */
    struct ip_addr csum_src = src;
    if (ip_is_zero(csum_src)) {
        struct ip_addr next_hop;
        struct ip_route *r = route_lookup(dst, &next_hop);
        struct netif *nif = r ? r->nif : netif_for_dest(dst);
        if (nif == NULL) {
            nif = netif_default();
        }
        if (nif != NULL) {
            csum_src = nif->ip;
        }
    }

    u16 total = (u16)(UDP_HDR_LEN + len);
    u16 csum = net_transport_checksum(csum_src.be, dst.be, IP_PROTO_UDP,
                                      nb->data, total);
    /* UDP uses 0xFFFF in place of a computed zero. */
    if (csum == 0) {
        csum = 0xFFFF;
    }
    uh->checksum = csum;

    return ipv4_output(nb, src, dst, IP_PROTO_UDP, 64);
}

void udp_input(struct netif *nif, struct netbuf *nb,
               struct ip_addr src, struct ip_addr dst) {
    (void)nif;
    if (netbuf_len(nb) < UDP_HDR_LEN) {
        return;
    }
    const struct udp_header *uh = (const struct udp_header *)nb->data;
    u16 ulen = net_get_be16(&uh->length);
    if (ulen < UDP_HDR_LEN || ulen > netbuf_len(nb)) {
        return;
    }

    /* Verify the checksum when present. */
    if (uh->checksum != 0) {
        u16 c = net_transport_checksum(src.be, dst.be, IP_PROTO_UDP,
                                       nb->data, ulen);
        if (c != 0) {
            return;   /* bad checksum */
        }
    }

    u16 src_port = net_get_be16(&uh->src_port);
    u16 dst_port = net_get_be16(&uh->dst_port);
    u16 plen = (u16)(ulen - UDP_HDR_LEN);
    const void *payload = (const u8 *)nb->data + UDP_HDR_LEN;

    g_datagrams_received++;

    struct udp_binding *b = udp_find(dst, dst_port);
    if (b != NULL && b->recv != NULL) {
        b->recv(b->ctx, src, src_port, dst, dst_port, payload, plen);
    }
}
