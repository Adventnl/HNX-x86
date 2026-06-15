/* ICMP echo (ping) implementation. */
#include "icmp.h"
#include "ipv4.h"
#include "checksum.h"
#include "net_endian.h"
#include "string.h"

static u64 g_echo_requests_seen;
static u64 g_echo_replies_sent;
static u64 g_echo_replies_seen;

void icmp_init(void) {
    g_echo_requests_seen = 0;
    g_echo_replies_sent = 0;
    g_echo_replies_seen = 0;
}

u64 icmp_echo_requests_seen(void) { return g_echo_requests_seen; }
u64 icmp_echo_replies_sent(void)  { return g_echo_replies_sent; }
u64 icmp_echo_replies_seen(void)  { return g_echo_replies_seen; }

/* Build an ICMP message into a fresh netbuf: type/code/id/seq + payload, then
 * checksum. Leaves nb->data at the ICMP header, headroom reserved for IP+Eth. */
static struct netbuf *icmp_build(u8 type, u8 code, u16 id, u16 seq,
                                 const void *payload, u16 len) {
    size_t cap = ICMP_HDR_LEN + len;
    struct netbuf *nb =
        netbuf_alloc_headroom(cap, NETBUF_DEFAULT_HEADROOM);
    if (nb == NULL) {
        return NULL;
    }
    struct icmp_header *ih =
        (struct icmp_header *)netbuf_put(nb, ICMP_HDR_LEN);
    if (ih == NULL) {
        netbuf_free(nb);
        return NULL;
    }
    ih->type = type;
    ih->code = code;
    ih->checksum = 0;
    net_put_be16(&ih->id, id);
    net_put_be16(&ih->seq, seq);
    if (len > 0) {
        uint8_t *body = netbuf_put(nb, len);
        if (body == NULL) {
            netbuf_free(nb);
            return NULL;
        }
        if (payload != NULL) {
            memcpy(body, payload, len);
        } else {
            memset(body, 0, len);
        }
    }
    ih->checksum = net_checksum(nb->data, netbuf_len(nb));
    return nb;
}

int icmp_send_echo(struct ip_addr dst, u16 id, u16 seq,
                   const void *payload, u16 len) {
    struct netbuf *nb = icmp_build(ICMP_ECHO_REQUEST, 0, id, seq, payload, len);
    if (nb == NULL) {
        return -1;
    }
    return ipv4_output(nb, IP_ADDR_ANY, dst, IP_PROTO_ICMP, 64);
}

void icmp_input(struct netif *nif, struct netbuf *nb,
                struct ip_addr src, struct ip_addr dst) {
    (void)nif;
    if (netbuf_len(nb) < ICMP_HDR_LEN) {
        return;
    }
    /* Validate the ICMP checksum (a good message sums to zero). */
    if (net_checksum(nb->data, netbuf_len(nb)) != 0) {
        return;
    }
    const struct icmp_header *ih = (const struct icmp_header *)nb->data;
    u8 type = ih->type;
    u16 id = net_get_be16(&ih->id);
    u16 seq = net_get_be16(&ih->seq);

    if (type == ICMP_ECHO_REPLY) {
        g_echo_replies_seen++;
        return;
    }
    if (type != ICMP_ECHO_REQUEST) {
        return;
    }
    g_echo_requests_seen++;

    /* Echo back the payload that followed the header. */
    size_t total = netbuf_len(nb);
    size_t plen = total - ICMP_HDR_LEN;
    const void *payload = (const u8 *)nb->data + ICMP_HDR_LEN;

    struct netbuf *reply =
        icmp_build(ICMP_ECHO_REPLY, 0, id, seq, payload, (u16)plen);
    if (reply == NULL) {
        return;
    }
    /* Reply to the original source, sourcing from the address it targeted. */
    if (ipv4_output(reply, dst, src, IP_PROTO_ICMP, 64) >= 0) {
        g_echo_replies_sent++;
    }
}
