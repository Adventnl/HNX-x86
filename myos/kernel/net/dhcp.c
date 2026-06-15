/* DHCP client state machine implementation. */
#include "dhcp.h"
#include "udp.h"
#include "ipv4.h"
#include "net_endian.h"
#include "string.h"

void dhcp_client_init(struct dhcp_client *c, struct netif *nif) {
    memset(c, 0, sizeof(*c));
    c->nif = nif;
    c->state = DHCP_STATE_INIT;
    c->xid = 0x4D594F53u;   /* "MYOS" — deterministic for tests */
}

/* Append a TLV option; returns the new write offset. */
static size_t opt_put(u8 *opts, size_t off, u8 code, u8 len, const void *val) {
    opts[off++] = code;
    opts[off++] = len;
    if (len > 0 && val != NULL) {
        memcpy(&opts[off], val, len);
    }
    return off + len;
}

size_t dhcp_build(struct dhcp_client *c, u8 msg_type, struct dhcp_message *out) {
    memset(out, 0, sizeof(*out));
    out->op = DHCP_OP_REQUEST;
    out->htype = DHCP_HTYPE_ETHER;
    out->hlen = ETH_ALEN;
    out->hops = 0;
    net_put_be32(&out->xid, c->xid);
    out->secs = 0;
    /* Request a broadcast reply (we have no IP yet). */
    net_put_be16(&out->flags, 0x8000u);
    memcpy(out->chaddr, c->nif->mac.addr, ETH_ALEN);
    net_put_be32(&out->magic, DHCP_MAGIC);

    size_t off = 0;
    u8 *o = out->options;
    off = opt_put(o, off, DHCP_OPT_MSG_TYPE, 1, &msg_type);

    if (msg_type == DHCP_REQUEST) {
        u32 req = c->offered_ip.be;     /* network order */
        off = opt_put(o, off, DHCP_OPT_REQUESTED_IP, 4, &req);
        u32 sid = c->server_id.be;
        off = opt_put(o, off, DHCP_OPT_SERVER_ID, 4, &sid);
    }

    /* Parameter request list: subnet, router, dns. */
    u8 params[3] = { DHCP_OPT_SUBNET_MASK, DHCP_OPT_ROUTER, DHCP_OPT_DNS };
    off = opt_put(o, off, DHCP_OPT_PARAM_LIST, sizeof(params), params);

    o[off++] = DHCP_OPT_END;

    /* Encoded length = fixed part up to options + option bytes written. */
    size_t fixed = sizeof(struct dhcp_message) - sizeof(out->options);
    return fixed + off;
}

u8 dhcp_parse(struct dhcp_client *c, const struct dhcp_message *msg,
              size_t len) {
    size_t fixed = sizeof(struct dhcp_message) - sizeof(msg->options);
    if (len < fixed + 1) {
        return 0;
    }
    if (net_get_be32(&msg->magic) != DHCP_MAGIC) {
        return 0;
    }
    if (msg->op != DHCP_OP_REPLY) {
        return 0;
    }

    c->offered_ip.be = msg->yiaddr;   /* yiaddr is already network order */

    size_t opt_len = len - fixed;
    if (opt_len > sizeof(msg->options)) {
        opt_len = sizeof(msg->options);
    }
    const u8 *o = msg->options;
    size_t i = 0;
    u8 msg_type = 0;

    while (i < opt_len) {
        u8 code = o[i++];
        if (code == DHCP_OPT_END) {
            break;
        }
        if (code == DHCP_OPT_PAD) {
            continue;
        }
        if (i >= opt_len) {
            break;
        }
        u8 olen = o[i++];
        if (i + olen > opt_len) {
            break;
        }
        const u8 *v = &o[i];
        switch (code) {
        case DHCP_OPT_MSG_TYPE:
            if (olen >= 1) {
                msg_type = v[0];
            }
            break;
        case DHCP_OPT_SUBNET_MASK:
            if (olen >= 4) {
                memcpy(&c->subnet_mask.be, v, 4);
            }
            break;
        case DHCP_OPT_ROUTER:
            if (olen >= 4) {
                memcpy(&c->router.be, v, 4);
            }
            break;
        case DHCP_OPT_DNS:
            if (olen >= 4) {
                memcpy(&c->dns.be, v, 4);
            }
            break;
        case DHCP_OPT_SERVER_ID:
            if (olen >= 4) {
                memcpy(&c->server_id.be, v, 4);
            }
            break;
        case DHCP_OPT_LEASE_TIME:
            if (olen >= 4) {
                c->lease_secs = net_get_be32(v);
            }
            break;
        default:
            break;
        }
        i += olen;
    }
    return msg_type;
}

static int dhcp_send(struct dhcp_client *c, u8 msg_type) {
    struct dhcp_message msg;
    size_t len = dhcp_build(c, msg_type, &msg);
    /* Broadcast from 0.0.0.0:68 to 255.255.255.255:67. */
    return udp_output(IP_ADDR_ANY, DHCP_CLIENT_PORT,
                      IP_ADDR_BROADCAST, DHCP_SERVER_PORT,
                      &msg, (u16)len);
}

int dhcp_start(struct dhcp_client *c) {
    c->state = DHCP_STATE_SELECTING;
    return dhcp_send(c, DHCP_DISCOVER);
}

u8 dhcp_input(struct dhcp_client *c, const void *data, size_t len) {
    u8 type = dhcp_parse(c, (const struct dhcp_message *)data, len);
    if (type == 0) {
        return 0;
    }
    switch (c->state) {
    case DHCP_STATE_SELECTING:
        if (type == DHCP_OFFER) {
            c->state = DHCP_STATE_REQUESTING;
            dhcp_send(c, DHCP_REQUEST);
        }
        break;
    case DHCP_STATE_REQUESTING:
        if (type == DHCP_ACK) {
            c->state = DHCP_STATE_BOUND;
            /* Commit the lease onto the interface. */
            if (c->nif != NULL && !ip_is_zero(c->offered_ip)) {
                struct ip_addr mask = c->subnet_mask;
                if (ip_is_zero(mask)) {
                    mask = ip_make(255, 255, 255, 0);
                }
                netif_set_addr(c->nif, c->offered_ip, mask, c->router);
            }
        } else if (type == DHCP_NAK) {
            c->state = DHCP_STATE_INIT;
        }
        break;
    default:
        break;
    }
    return type;
}
