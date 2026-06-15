/* DHCP client state machine (DISCOVER / OFFER / REQUEST / ACK). */
#ifndef MYOS_NET_DHCP_H
#define MYOS_NET_DHCP_H

#include "types.h"
#include "net_addr.h"
#include "net_if.h"
#include "ethernet.h"

/* BOOTP / DHCP message (fixed part; options follow). */
struct dhcp_message {
    u8  op;          /* 1 = BOOTREQUEST, 2 = BOOTREPLY */
    u8  htype;       /* 1 = Ethernet */
    u8  hlen;        /* 6 */
    u8  hops;
    u32 xid;         /* transaction id (big-endian on wire) */
    u16 secs;
    u16 flags;
    u32 ciaddr;      /* client IP (big-endian) */
    u32 yiaddr;      /* "your" IP offered to client */
    u32 siaddr;      /* next server IP */
    u32 giaddr;      /* relay agent IP */
    u8  chaddr[16];  /* client hardware address */
    u8  sname[64];
    u8  file[128];
    u32 magic;       /* magic cookie 0x63825363 (big-endian) */
    u8  options[312];
} __attribute__((packed));

#define DHCP_OP_REQUEST  1u
#define DHCP_OP_REPLY    2u
#define DHCP_HTYPE_ETHER 1u
#define DHCP_MAGIC       0x63825363u

#define DHCP_SERVER_PORT 67u
#define DHCP_CLIENT_PORT 68u

/* Option codes. */
#define DHCP_OPT_PAD            0u
#define DHCP_OPT_SUBNET_MASK    1u
#define DHCP_OPT_ROUTER         3u
#define DHCP_OPT_DNS            6u
#define DHCP_OPT_REQUESTED_IP   50u
#define DHCP_OPT_LEASE_TIME     51u
#define DHCP_OPT_MSG_TYPE       53u
#define DHCP_OPT_SERVER_ID      54u
#define DHCP_OPT_PARAM_LIST     55u
#define DHCP_OPT_END            255u

/* DHCP message types (option 53). */
#define DHCP_DISCOVER 1u
#define DHCP_OFFER    2u
#define DHCP_REQUEST  3u
#define DHCP_DECLINE  4u
#define DHCP_ACK      5u
#define DHCP_NAK      6u
#define DHCP_RELEASE  7u

/* Client state machine. */
enum dhcp_state {
    DHCP_STATE_INIT = 0,
    DHCP_STATE_SELECTING,   /* DISCOVER sent, awaiting OFFER */
    DHCP_STATE_REQUESTING,  /* REQUEST sent, awaiting ACK    */
    DHCP_STATE_BOUND,
};

struct dhcp_client {
    struct netif   *nif;
    enum dhcp_state state;
    u32             xid;
    struct ip_addr  offered_ip;
    struct ip_addr  server_id;
    struct ip_addr  subnet_mask;
    struct ip_addr  router;
    struct ip_addr  dns;
    u32             lease_secs;
};

void dhcp_client_init(struct dhcp_client *c, struct netif *nif);

/* Kick off the lease: build + send a DISCOVER, move to SELECTING. */
int  dhcp_start(struct dhcp_client *c);

/* Build a DHCP message of `msg_type` into `out` (caller-provided). Returns the
 * total encoded length (fixed part + options). */
size_t dhcp_build(struct dhcp_client *c, u8 msg_type, struct dhcp_message *out);

/* Parse a DHCP message: extract the message type and the common options into
 * the client. Returns the message type, or 0 if malformed. */
u8   dhcp_parse(struct dhcp_client *c, const struct dhcp_message *msg,
                size_t len);

/* Feed a received DHCP datagram (UDP payload) to the state machine. Advances
 * SELECTING->REQUESTING (on OFFER) and REQUESTING->BOUND (on ACK). Returns the
 * message type handled, or 0. */
u8   dhcp_input(struct dhcp_client *c, const void *data, size_t len);

#endif /* MYOS_NET_DHCP_H */
