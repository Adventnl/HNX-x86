/* Ethernet II framing implementation. */
#include "ethernet.h"
#include "net_endian.h"
#include "string.h"

const struct mac_addr eth_broadcast = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
};
const struct mac_addr eth_zero = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};

int mac_is_broadcast(const struct mac_addr *m) {
    return mac_equal(m, &eth_broadcast);
}

int mac_is_multicast(const struct mac_addr *m) {
    /* Group bit of the first octet. */
    return (m->addr[0] & 0x01u) != 0;
}

int mac_is_zero(const struct mac_addr *m) {
    return mac_equal(m, &eth_zero);
}

void mac_copy(struct mac_addr *dst, const struct mac_addr *src) {
    memcpy(dst->addr, src->addr, ETH_ALEN);
}

struct mac_addr mac_make(u8 a, u8 b, u8 c, u8 d, u8 e, u8 f) {
    struct mac_addr m;
    m.addr[0] = a; m.addr[1] = b; m.addr[2] = c;
    m.addr[3] = d; m.addr[4] = e; m.addr[5] = f;
    return m;
}

int eth_build(struct netbuf *nb, const struct mac_addr *dst,
              const struct mac_addr *src, u16 ethertype) {
    uint8_t *p = netbuf_push(nb, ETH_HLEN);
    if (p == NULL) {
        return -1;
    }
    struct eth_header *eh = (struct eth_header *)p;
    memcpy(eh->dst, dst->addr, ETH_ALEN);
    memcpy(eh->src, src->addr, ETH_ALEN);
    net_put_be16(&eh->ethertype, ethertype);
    nb->eth = p;
    return 0;
}

int eth_parse(struct netbuf *nb, struct mac_addr *dst, struct mac_addr *src,
              u16 *ethertype) {
    if (netbuf_len(nb) < ETH_HLEN) {
        return -1;
    }
    const struct eth_header *eh = (const struct eth_header *)nb->data;
    nb->eth = nb->data;
    if (dst != NULL) {
        memcpy(dst->addr, eh->dst, ETH_ALEN);
    }
    if (src != NULL) {
        memcpy(src->addr, eh->src, ETH_ALEN);
    }
    if (ethertype != NULL) {
        *ethertype = net_get_be16(&eh->ethertype);
    }
    netbuf_pull(nb, ETH_HLEN);
    return 0;
}
