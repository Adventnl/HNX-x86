/* DNS implementation: name codec, query/response builder + parser, resolver. */
#include "dns.h"
#include "udp.h"
#include "net_endian.h"
#include "string.h"

#define DNS_HDR_LEN ((size_t)sizeof(struct dns_header))

void dns_init(void) {
    /* Nothing global to set up; resolver state is per-call. */
}

size_t dns_encode_name(const char *name, u8 *buf, size_t cap) {
    size_t out = 0;
    size_t i = 0;

    while (name[i] != '\0') {
        /* Find the end of this label. */
        size_t start = i;
        while (name[i] != '\0' && name[i] != '.') {
            i++;
        }
        size_t label_len = i - start;
        if (label_len == 0 || label_len > 63) {
            return 0;
        }
        if (out + 1 + label_len >= cap) {
            return 0;
        }
        buf[out++] = (u8)label_len;
        for (size_t j = 0; j < label_len; j++) {
            buf[out++] = (u8)name[start + j];
        }
        if (name[i] == '.') {
            i++;
        }
    }
    if (out + 1 > cap) {
        return 0;
    }
    buf[out++] = 0;   /* root label */
    return out;
}

size_t dns_build_query(u16 id, const char *name, u16 qtype,
                       u8 *buf, size_t cap) {
    if (cap < DNS_HDR_LEN) {
        return 0;
    }
    struct dns_header *h = (struct dns_header *)buf;
    net_put_be16(&h->id, id);
    net_put_be16(&h->flags, DNS_FLAG_RD);
    net_put_be16(&h->qdcount, 1);
    net_put_be16(&h->ancount, 0);
    net_put_be16(&h->nscount, 0);
    net_put_be16(&h->arcount, 0);

    size_t off = DNS_HDR_LEN;
    size_t nlen = dns_encode_name(name, buf + off, cap - off);
    if (nlen == 0) {
        return 0;
    }
    off += nlen;
    if (off + 4 > cap) {
        return 0;
    }
    net_put_be16(buf + off, qtype); off += 2;
    net_put_be16(buf + off, DNS_CLASS_IN); off += 2;
    return off;
}

/* Skip a (possibly compressed) name starting at `off`. Returns the offset just
 * past the name in the linear stream, or 0 on error. */
static size_t dns_skip_name(const u8 *buf, size_t len, size_t off) {
    while (off < len) {
        u8 b = buf[off];
        if (b == 0) {
            return off + 1;
        }
        if ((b & 0xC0u) == 0xC0u) {
            /* Compression pointer: two bytes, terminates the name. */
            return off + 2;
        }
        if ((b & 0xC0u) != 0) {
            return 0;   /* reserved label type */
        }
        off += 1u + b;
    }
    return 0;
}

int dns_parse_response(const u8 *buf, size_t len, u16 match_id,
                       struct ip_addr *out) {
    if (len < DNS_HDR_LEN) {
        return 0;
    }
    const struct dns_header *h = (const struct dns_header *)buf;
    u16 id = net_get_be16(&h->id);
    u16 flags = net_get_be16(&h->flags);
    if (match_id != 0 && id != match_id) {
        return 0;
    }
    if (!(flags & DNS_FLAG_QR)) {
        return 0;   /* not a response */
    }
    if ((flags & DNS_RCODE_MASK) != 0) {
        return 0;   /* server error */
    }
    u16 qd = net_get_be16(&h->qdcount);
    u16 an = net_get_be16(&h->ancount);
    if (an == 0) {
        return 0;
    }

    size_t off = DNS_HDR_LEN;

    /* Skip the question section. */
    for (u16 q = 0; q < qd; q++) {
        off = dns_skip_name(buf, len, off);
        if (off == 0 || off + 4 > len) {
            return 0;
        }
        off += 4;   /* qtype + qclass */
    }

    /* Walk answers, looking for the first A record. */
    for (u16 a = 0; a < an; a++) {
        off = dns_skip_name(buf, len, off);
        if (off == 0 || off + 10 > len) {
            return 0;
        }
        u16 type = net_get_be16(buf + off);
        /* class at +2 (ignored), ttl at +4 (4 bytes), rdlength at +8. */
        u16 rdlen = net_get_be16(buf + off + 8);
        off += 10;
        if (off + rdlen > len) {
            return 0;
        }
        if (type == DNS_TYPE_A && rdlen == 4) {
            if (out != NULL) {
                memcpy(&out->be, buf + off, 4);
            }
            return 1;
        }
        off += rdlen;
    }
    return 0;
}

/* Per-resolution context kept alive by the caller (out/done pointers). */
struct dns_resolution {
    u16             id;
    struct ip_addr *out;
    volatile int   *done;
};

static struct dns_resolution g_resolution;   /* one in-flight query (foundation) */

static void dns_recv(void *ctx, struct ip_addr src, u16 src_port,
                     struct ip_addr dst, u16 dst_port,
                     const void *data, u16 len) {
    (void)src; (void)src_port; (void)dst; (void)dst_port;
    struct dns_resolution *r = (struct dns_resolution *)ctx;
    struct ip_addr addr;
    if (dns_parse_response((const u8 *)data, len, r->id, &addr) == 1) {
        if (r->out) {
            *r->out = addr;
        }
        if (r->done) {
            *r->done = 1;
        }
    }
}

int dns_resolve(struct ip_addr server, const char *name,
                struct ip_addr *out, volatile int *done) {
    u8 query[DNS_MAX_PACKET];
    static u16 next_id = 0x1000;
    u16 id = next_id++;

    size_t qlen = dns_build_query(id, name, DNS_TYPE_A, query, sizeof(query));
    if (qlen == 0) {
        return -1;
    }

    g_resolution.id = id;
    g_resolution.out = out;
    g_resolution.done = done;
    if (done) {
        *done = 0;
    }

    u16 lport = udp_ephemeral_port();
    udp_bind(IP_ADDR_ANY, lport, dns_recv, &g_resolution);

    int rc = udp_output(IP_ADDR_ANY, lport, server, DNS_PORT,
                        query, (u16)qlen);
    if (rc < 0) {
        udp_unbind(IP_ADDR_ANY, lport);
        return -1;
    }
    return 0;
}
