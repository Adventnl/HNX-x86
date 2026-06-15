/* Networking subsystem self-tests (deterministic, loopback-only).
 *
 * Each protocol layer is exercised by injecting/building real packets and
 * validating parsing, checksums and state-machine transitions. No real network
 * is touched: ICMP/UDP traffic runs through the loopback interface, the NIC
 * driver is brought up against a RAM-backed register window, and DHCP/DNS are
 * driven by hand-built messages.
 *
 * Markers (consumed by the verify target):
 *   [OK] NIC driver
 *   [PASS] Ethernet
 *   [PASS] ARP
 *   [PASS] IPv4
 *   [PASS] ICMP ping
 *   [PASS] UDP
 *   [PASS] DHCP
 *   [PASS] DNS
 *   [PASS] socket API
 *   [OK] Networking foundation online
 */
#include "ktest.h"
#include "net.h"
#include "slab.h"
#include "string.h"
#include "log.h"
#include "fmt.h"

/* ---- [OK] NIC driver ------------------------------------------------------
 * Bring the e1000 up against a RAM-backed register window (no PCI / no real
 * device): verifies reset, MAC read from RAL/RAH, ring construction and the TX
 * descriptor path. */
static int nic_driver_test(void) {
    /* 64 KiB covers the e1000 register file used here. */
    const size_t MMIO_SIZE = 0x10000u;
    u8 *mmio = (u8 *)kmem_zalloc(MMIO_SIZE);
    if (mmio == NULL) {
        return 0;
    }

    /* Seed RAL0/RAH0 so the no-EEPROM MAC read returns a known address. */
    u8 want[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    *(volatile u32 *)(mmio + E1000_REG_RAL0) =
        (u32)want[0] | ((u32)want[1] << 8) |
        ((u32)want[2] << 16) | ((u32)want[3] << 24);
    *(volatile u32 *)(mmio + E1000_REG_RAH0) =
        (u32)want[4] | ((u32)want[5] << 8);

    static struct e1000_device dev;
    memset(&dev, 0, sizeof(dev));
    int ok = 1;

    if (e1000_bringup(&dev, (volatile u8 *)mmio, "test0") != 0) {
        kmem_free(mmio);
        return 0;
    }

    /* MAC came back from RAL/RAH. */
    ok &= (memcmp(dev.mac.addr, want, 6) == 0);
    /* Rings were allocated and registers programmed. */
    ok &= (dev.rx_ring != NULL && dev.tx_ring != NULL);
    ok &= (*(volatile u32 *)(mmio + E1000_REG_RDLEN) ==
           E1000_NUM_RX_DESC * (u32)sizeof(struct e1000_rx_desc));
    ok &= (*(volatile u32 *)(mmio + E1000_REG_TDLEN) ==
           E1000_NUM_TX_DESC * (u32)sizeof(struct e1000_tx_desc));

    /* Transmit a frame through the TX ring and confirm the descriptor. */
    u8 frame[64];
    memset(frame, 0xA5, sizeof(frame));
    unsigned slot = dev.tx_cur;
    int tx = e1000_tx_raw(&dev, frame, sizeof(frame));
    ok &= (tx == 0);
    ok &= (dev.tx_ring[slot].length == sizeof(frame));
    ok &= ((dev.tx_ring[slot].cmd & E1000_TXD_CMD_EOP) != 0);
    /* TDT advanced past the posted descriptor. */
    ok &= (*(volatile u32 *)(mmio + E1000_REG_TDT) == dev.tx_cur);

    /* Tear the test interface back down so it does not linger in the registry
     * (we leave it registered but down; registry has room). */
    netif_down(&dev.nif);
    kmem_free(mmio);
    return ok;
}

static void test_nic(void) {
    if (nic_driver_test()) {
        kernel_log_ok("NIC driver");
    } else {
        kernel_log_error("NIC driver");
    }
}

/* ---- [PASS] Ethernet ------------------------------------------------------ */
static void test_ethernet(void) {
    KT_BEGIN();
    struct mac_addr dmac = mac_make(0x02, 0x00, 0x00, 0x00, 0x00, 0x01);
    struct mac_addr smac = mac_make(0x02, 0x00, 0x00, 0x00, 0x00, 0x02);

    struct netbuf *nb = netbuf_alloc(64);
    KT_CHECK(nb != NULL, "netbuf alloc");
    if (nb) {
        /* Put a 4-byte payload, then frame it. */
        u8 payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        netbuf_put_data(nb, payload, sizeof(payload));
        KT_CHECK_EQ(eth_build(nb, &dmac, &smac, ETH_P_IPV4), 0, "eth_build");
        KT_CHECK_EQ(netbuf_len(nb), ETH_HLEN + 4, "framed length");

        /* Parse it back. */
        struct mac_addr pdst, psrc;
        u16 et = 0;
        KT_CHECK_EQ(eth_parse(nb, &pdst, &psrc, &et), 0, "eth_parse");
        KT_CHECK_EQ(et, ETH_P_IPV4, "ethertype");
        KT_CHECK(mac_equal(&pdst, &dmac), "dst mac roundtrip");
        KT_CHECK(mac_equal(&psrc, &smac), "src mac roundtrip");
        KT_CHECK_EQ(netbuf_len(nb), 4, "payload remains after parse");
        netbuf_free(nb);
    }

    KT_CHECK(mac_is_broadcast(&eth_broadcast), "broadcast detect");
    KT_CHECK(!mac_is_broadcast(&smac), "unicast not broadcast");
    KT_CHECK(mac_is_multicast(&eth_broadcast), "broadcast is multicast");

    KT_RESULT("Ethernet");
}

/* ---- [PASS] ARP ----------------------------------------------------------- */
/* Build an ARP packet into a netbuf (header only, no Ethernet). */
static struct netbuf *build_arp(u16 oper, struct ip_addr spa, struct mac_addr sha,
                                struct ip_addr tpa, struct mac_addr tha) {
    struct netbuf *nb = netbuf_alloc(ARP_HDR_LEN);
    if (!nb) {
        return NULL;
    }
    struct arp_header *ah = (struct arp_header *)netbuf_put(nb, ARP_HDR_LEN);
    net_put_be16(&ah->htype, ARP_HTYPE_ETHER);
    net_put_be16(&ah->ptype, ARP_PTYPE_IPV4);
    ah->hlen = ETH_ALEN;
    ah->plen = 4;
    net_put_be16(&ah->oper, oper);
    memcpy(ah->sha, sha.addr, ETH_ALEN);
    net_put_be32(ah->spa, ip_to_host(spa));
    memcpy(ah->tha, tha.addr, ETH_ALEN);
    net_put_be32(ah->tpa, ip_to_host(tpa));
    return nb;
}

static void test_arp(void) {
    KT_BEGIN();
    arp_init();

    /* A test interface so arp_input can reply to requests for our IP. */
    static struct netif tnif;
    memset(&tnif, 0, sizeof(tnif));
    strlcpy(tnif.name, "arp0", NETIF_NAME_MAX);
    tnif.mac = mac_make(0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE);
    netif_set_addr(&tnif, ip_make(192, 168, 1, 1),
                   ip_make(255, 255, 255, 0), IP_ADDR_ANY);
    tnif.tx = NULL;   /* drop replies; we only test cache learning here */

    struct ip_addr peer_ip = ip_make(192, 168, 1, 50);
    struct mac_addr peer_mac = mac_make(0x02, 0x11, 0x22, 0x33, 0x44, 0x55);

    /* Inject an ARP reply: the sender mapping must be learned. */
    struct netbuf *reply = build_arp(ARP_OP_REPLY, peer_ip, peer_mac,
                                     tnif.ip, tnif.mac);
    KT_CHECK(reply != NULL, "arp reply built");
    if (reply) {
        arp_input(&tnif, reply, &peer_mac);
        netbuf_free(reply);
    }

    struct mac_addr got;
    KT_CHECK_EQ(arp_cache_lookup(peer_ip, &got), 0, "peer resolved in cache");
    KT_CHECK(mac_equal(&got, &peer_mac), "cached mac matches");

    /* Manual insert + lookup. */
    struct ip_addr ip2 = ip_make(192, 168, 1, 99);
    struct mac_addr mac2 = mac_make(0x06, 0x05, 0x04, 0x03, 0x02, 0x01);
    arp_cache_insert(ip2, &mac2);
    KT_CHECK_EQ(arp_cache_lookup(ip2, &got), 0, "manual insert resolves");
    KT_CHECK(mac_equal(&got, &mac2), "manual mac matches");

    /* Unknown address does not resolve. */
    KT_CHECK(arp_cache_lookup(ip_make(10, 10, 10, 10), &got) != 0,
             "unknown not resolved");

    KT_RESULT("ARP");
}

/* ---- [PASS] IPv4 ---------------------------------------------------------- */
static void test_ipv4(void) {
    KT_BEGIN();

    struct ip_addr src = ip_make(192, 168, 0, 1);
    struct ip_addr dst = ip_make(192, 168, 0, 2);

    struct netbuf *nb = netbuf_alloc(64);
    KT_CHECK(nb != NULL, "netbuf alloc");
    if (nb) {
        u8 payload[8];
        memset(payload, 0x5A, sizeof(payload));
        netbuf_put_data(nb, payload, sizeof(payload));
        KT_CHECK_EQ(ipv4_build(nb, src, dst, IP_PROTO_UDP, 64), 0, "ipv4_build");

        struct ipv4_header *h = (struct ipv4_header *)nb->data;
        KT_CHECK_EQ(ipv4_version(h), IPV4_VERSION, "version");
        KT_CHECK_EQ(ipv4_hdr_bytes(h), IPV4_HDR_LEN, "header length");
        KT_CHECK_EQ(net_get_be16(&h->total_len), IPV4_HDR_LEN + 8, "total len");
        KT_CHECK_EQ(h->proto, IP_PROTO_UDP, "protocol");
        /* A correct header checksums to zero. */
        KT_CHECK_EQ(net_checksum(h, IPV4_HDR_LEN), 0, "header checksum valid");
        KT_CHECK(h->src == src.be && h->dst == dst.be, "addresses");
        netbuf_free(nb);
    }

    /* Routing: install a route and confirm longest-prefix lookup. */
    route_init();
    static struct netif rnif;
    memset(&rnif, 0, sizeof(rnif));
    strlcpy(rnif.name, "rt0", NETIF_NAME_MAX);
    route_add(ip_make(10, 0, 0, 0), ip_make(255, 0, 0, 0), IP_ADDR_ANY, &rnif);
    route_add(ip_make(10, 1, 2, 0), ip_make(255, 255, 255, 0),
              ip_make(10, 1, 2, 254), &rnif);
    struct ip_addr nh;
    struct ip_route *r = route_lookup(ip_make(10, 1, 2, 9), &nh);
    KT_CHECK(r != NULL, "route found");
    KT_CHECK(r && r->prefix_len == 24, "longest prefix chosen");
    KT_CHECK(ip_equal(nh, ip_make(10, 1, 2, 254)), "next hop is gateway");

    struct ip_route *r2 = route_lookup(ip_make(10, 9, 9, 9), &nh);
    KT_CHECK(r2 != NULL && r2->prefix_len == 8, "fallback to /8 route");

    KT_RESULT("IPv4");
}

/* ---- [PASS] ICMP ping ----------------------------------------------------- */
static void test_icmp(void) {
    KT_BEGIN();

    /* Fresh stack state over loopback. */
    netif_registry_init();
    arp_init();
    route_init();
    ipv4_init();
    icmp_init();
    udp_init();

    struct netif *lo = loopback_init();
    KT_CHECK(lo != NULL, "loopback up");
    route_add(ip_make(127, 0, 0, 0), ip_make(255, 0, 0, 0), IP_ADDR_ANY, lo);

    u64 req0 = icmp_echo_requests_seen();
    u64 rep0 = icmp_echo_replies_sent();
    u64 seen0 = icmp_echo_replies_seen();

    /* Ping 127.0.0.1: request loops to icmp_input which builds a reply that
     * loops back again and is observed as an echo reply. */
    u8 ping_payload[16];
    for (unsigned i = 0; i < sizeof(ping_payload); i++) {
        ping_payload[i] = (u8)(0x40 + i);
    }
    int rc = icmp_send_echo(IP_ADDR_LOOPBACK, 0x1234, 1,
                            ping_payload, sizeof(ping_payload));
    KT_CHECK(rc >= 0, "echo request sent");

    KT_CHECK(icmp_echo_requests_seen() == req0 + 1,
             "echo request received on loopback");
    KT_CHECK(icmp_echo_replies_sent() == rep0 + 1, "echo reply generated");
    KT_CHECK(icmp_echo_replies_seen() == seen0 + 1,
             "echo reply received back");

    KT_RESULT("ICMP ping");
}

/* ---- [PASS] UDP ----------------------------------------------------------- */
static volatile int   g_udp_got;
static u8             g_udp_buf[64];
static u16            g_udp_len;
static u16            g_udp_src_port;

static void udp_test_recv(void *ctx, struct ip_addr src, u16 src_port,
                          struct ip_addr dst, u16 dst_port,
                          const void *data, u16 len) {
    (void)ctx; (void)src; (void)dst; (void)dst_port;
    g_udp_src_port = src_port;
    g_udp_len = len;
    if (len <= sizeof(g_udp_buf)) {
        memcpy(g_udp_buf, data, len);
    }
    g_udp_got = 1;
}

static void test_udp(void) {
    KT_BEGIN();

    /* Loopback stack. */
    netif_registry_init();
    arp_init();
    route_init();
    ipv4_init();
    icmp_init();
    udp_init();
    struct netif *lo = loopback_init();
    route_add(ip_make(127, 0, 0, 0), ip_make(255, 0, 0, 0), IP_ADDR_ANY, lo);

    g_udp_got = 0;
    g_udp_len = 0;
    KT_CHECK_EQ(udp_bind(IP_ADDR_LOOPBACK, 7777, udp_test_recv, NULL), 0,
                "udp bind");

    const char *msg = "hello-udp";
    u16 mlen = (u16)strlen(msg);
    int rc = udp_output(IP_ADDR_LOOPBACK, 4444, IP_ADDR_LOOPBACK, 7777,
                        msg, mlen);
    KT_CHECK(rc >= 0, "udp_output ok");
    KT_CHECK(g_udp_got == 1, "datagram delivered via loopback");
    KT_CHECK_EQ(g_udp_len, mlen, "payload length preserved");
    KT_CHECK(memcmp(g_udp_buf, msg, mlen) == 0, "payload bytes preserved");
    KT_CHECK_EQ(g_udp_src_port, 4444, "source port preserved");

    /* Duplicate bind on the same (ip,port) is rejected. */
    KT_CHECK(udp_bind(IP_ADDR_LOOPBACK, 7777, udp_test_recv, NULL) != 0,
             "duplicate bind rejected");

    udp_unbind(IP_ADDR_LOOPBACK, 7777);
    KT_RESULT("UDP");
}

/* ---- [PASS] DHCP ---------------------------------------------------------- */
/* Build a server reply (OFFER or ACK) into `out`. Returns total length. */
static size_t build_dhcp_reply(u8 msg_type, u32 xid, struct ip_addr yiaddr,
                               struct ip_addr server, struct ip_addr mask,
                               struct ip_addr router, struct dhcp_message *out) {
    memset(out, 0, sizeof(*out));
    out->op = DHCP_OP_REPLY;
    out->htype = DHCP_HTYPE_ETHER;
    out->hlen = ETH_ALEN;
    net_put_be32(&out->xid, xid);
    out->yiaddr = yiaddr.be;
    net_put_be32(&out->magic, DHCP_MAGIC);

    u8 *o = out->options;
    size_t off = 0;
    o[off++] = DHCP_OPT_MSG_TYPE; o[off++] = 1; o[off++] = msg_type;
    o[off++] = DHCP_OPT_SERVER_ID; o[off++] = 4;
    memcpy(&o[off], &server.be, 4); off += 4;
    o[off++] = DHCP_OPT_SUBNET_MASK; o[off++] = 4;
    memcpy(&o[off], &mask.be, 4); off += 4;
    o[off++] = DHCP_OPT_ROUTER; o[off++] = 4;
    memcpy(&o[off], &router.be, 4); off += 4;
    o[off++] = DHCP_OPT_END;

    size_t fixed = sizeof(struct dhcp_message) - sizeof(out->options);
    return fixed + off;
}

static void test_dhcp(void) {
    KT_BEGIN();

    static struct netif dnif;
    memset(&dnif, 0, sizeof(dnif));
    strlcpy(dnif.name, "dh0", NETIF_NAME_MAX);
    dnif.mac = mac_make(0x52, 0x54, 0x00, 0xAB, 0xCD, 0xEF);

    struct dhcp_client cli;
    dhcp_client_init(&cli, &dnif);
    KT_CHECK_EQ(cli.state, DHCP_STATE_INIT, "initial state");

    /* DISCOVER build: must encode our chaddr + a DISCOVER message type. */
    struct dhcp_message disc;
    size_t dlen = dhcp_build(&cli, DHCP_DISCOVER, &disc);
    KT_CHECK(dlen > 0, "discover built");
    KT_CHECK(memcmp(disc.chaddr, dnif.mac.addr, ETH_ALEN) == 0, "chaddr set");
    KT_CHECK_EQ(net_get_be32(&disc.magic), DHCP_MAGIC, "magic cookie");
    KT_CHECK_EQ(disc.options[0], DHCP_OPT_MSG_TYPE, "msgtype option first");
    KT_CHECK_EQ(disc.options[2], DHCP_DISCOVER, "discover type");

    /* Move to SELECTING, then feed an OFFER. */
    cli.state = DHCP_STATE_SELECTING;
    struct ip_addr offered = ip_make(192, 168, 1, 100);
    struct ip_addr server  = ip_make(192, 168, 1, 1);
    struct ip_addr mask    = ip_make(255, 255, 255, 0);
    struct ip_addr router  = ip_make(192, 168, 1, 1);

    struct dhcp_message offer;
    size_t olen = build_dhcp_reply(DHCP_OFFER, cli.xid, offered, server,
                                   mask, router, &offer);
    u8 t = dhcp_parse(&cli, &offer, olen);
    KT_CHECK_EQ(t, DHCP_OFFER, "offer parsed");
    KT_CHECK(ip_equal(cli.offered_ip, offered), "offered ip extracted");
    KT_CHECK(ip_equal(cli.server_id, server), "server id extracted");
    KT_CHECK(ip_equal(cli.subnet_mask, mask), "subnet mask extracted");

    /* Drive the state machine: OFFER -> REQUESTING. (No tx interface, so the
     * REQUEST send just no-ops/drops; the transition is what we assert.) */
    dnif.tx = NULL;
    u8 t2 = dhcp_input(&cli, &offer, olen);
    KT_CHECK_EQ(t2, DHCP_OFFER, "offer handled");
    KT_CHECK_EQ(cli.state, DHCP_STATE_REQUESTING, "moved to REQUESTING");

    /* Feed an ACK -> BOUND, and the lease commits onto the interface. */
    struct dhcp_message ack;
    size_t alen = build_dhcp_reply(DHCP_ACK, cli.xid, offered, server,
                                   mask, router, &ack);
    u8 t3 = dhcp_input(&cli, &ack, alen);
    KT_CHECK_EQ(t3, DHCP_ACK, "ack handled");
    KT_CHECK_EQ(cli.state, DHCP_STATE_BOUND, "moved to BOUND");
    KT_CHECK(ip_equal(dnif.ip, offered), "interface ip committed");

    KT_RESULT("DHCP");
}

/* ---- [PASS] DNS ----------------------------------------------------------- */
/* Hand-build a DNS A response for `name` -> `addr` with question echoed. */
static size_t build_dns_response(u16 id, const char *name, struct ip_addr addr,
                                 u8 *buf, size_t cap) {
    struct dns_header *h = (struct dns_header *)buf;
    net_put_be16(&h->id, id);
    net_put_be16(&h->flags, DNS_FLAG_QR | DNS_FLAG_RD | DNS_FLAG_RA);
    net_put_be16(&h->qdcount, 1);
    net_put_be16(&h->ancount, 1);
    net_put_be16(&h->nscount, 0);
    net_put_be16(&h->arcount, 0);

    size_t off = sizeof(struct dns_header);
    size_t nlen = dns_encode_name(name, buf + off, cap - off);
    if (nlen == 0) {
        return 0;
    }
    off += nlen;
    net_put_be16(buf + off, DNS_TYPE_A); off += 2;
    net_put_be16(buf + off, DNS_CLASS_IN); off += 2;

    /* Answer: compressed name pointer to the question (offset 12). */
    buf[off++] = 0xC0;
    buf[off++] = (u8)sizeof(struct dns_header);
    net_put_be16(buf + off, DNS_TYPE_A); off += 2;
    net_put_be16(buf + off, DNS_CLASS_IN); off += 2;
    net_put_be32(buf + off, 300); off += 4;            /* TTL */
    net_put_be16(buf + off, 4); off += 2;              /* RDLENGTH */
    memcpy(buf + off, &addr.be, 4); off += 4;          /* RDATA */
    return off;
}

static void test_dns(void) {
    KT_BEGIN();

    /* Build a query and sanity-check the encoded name. */
    u8 q[DNS_MAX_PACKET];
    size_t qlen = dns_build_query(0xBEEF, "www.example.com", DNS_TYPE_A,
                                  q, sizeof(q));
    KT_CHECK(qlen > sizeof(struct dns_header), "query built");
    /* First label length should be 3 ("www"). */
    KT_CHECK_EQ(q[sizeof(struct dns_header)], 3, "first label length");

    /* Build + parse a response. */
    struct ip_addr want = ip_make(93, 184, 216, 34);
    u8 resp[DNS_MAX_PACKET];
    memset(resp, 0, sizeof(resp));
    size_t rlen = build_dns_response(0xBEEF, "www.example.com", want,
                                     resp, sizeof(resp));
    KT_CHECK(rlen > 0, "response built");

    struct ip_addr got = IP_ADDR_ANY;
    int found = dns_parse_response(resp, rlen, 0xBEEF, &got);
    KT_CHECK_EQ(found, 1, "A record parsed");
    KT_CHECK(ip_equal(got, want), "resolved address matches");

    /* Wrong id is rejected. */
    KT_CHECK(dns_parse_response(resp, rlen, 0x1111, &got) == 0,
             "mismatched id rejected");

    KT_RESULT("DNS");
}

/* ---- [PASS] socket API ---------------------------------------------------- */
static void test_socket(void) {
    KT_BEGIN();

    /* Loopback stack + fresh socket table. */
    netif_registry_init();
    arp_init();
    route_init();
    ipv4_init();
    icmp_init();
    udp_init();
    net_socket_init();
    struct netif *lo = loopback_init();
    route_add(ip_make(127, 0, 0, 0), ip_make(255, 0, 0, 0), IP_ADDR_ANY, lo);

    int rx = sock_create(SOCK_DGRAM);
    int tx = sock_create(SOCK_DGRAM);
    KT_CHECK(rx >= 0 && tx >= 0, "sockets created");

    KT_CHECK_EQ(sock_bind(rx, IP_ADDR_LOOPBACK, 9001), 0, "bind receiver");
    KT_CHECK_EQ(sock_bind(tx, IP_ADDR_LOOPBACK, 9002), 0, "bind sender");

    const char *msg = "sock-payload";
    u16 mlen = (u16)strlen(msg);
    int sent = sock_sendto(tx, msg, mlen, IP_ADDR_LOOPBACK, 9001);
    KT_CHECK_EQ(sent, mlen, "sendto returns length");

    u8 buf[64];
    struct ip_addr from;
    u16 fport = 0;
    int got = sock_recvfrom(rx, buf, sizeof(buf), &from, &fport);
    KT_CHECK_EQ(got, mlen, "recvfrom length");
    KT_CHECK(memcmp(buf, msg, mlen) == 0, "recvfrom payload");
    KT_CHECK_EQ(fport, 9002, "source port observed");
    KT_CHECK(ip_equal(from, IP_ADDR_LOOPBACK), "source ip observed");

    /* Empty queue now returns -1. */
    KT_CHECK(sock_recvfrom(rx, buf, sizeof(buf), &from, &fport) < 0,
             "empty queue returns error");

    /* TCP foundation: a stream socket connect drives the state machine. */
    int st = sock_create(SOCK_STREAM);
    KT_CHECK(st >= 0, "stream socket created");
    struct socket *ss = sock_get(st);
    KT_CHECK(ss != NULL && ss->tcb != NULL, "stream socket has TCB");
    if (ss && ss->tcb) {
        sock_connect(st, IP_ADDR_LOOPBACK, 80);
        /* After connect the TCB has left CLOSED (SYN sent / handshake). */
        KT_CHECK(ss->tcb->state != TCP_CLOSED, "tcb left CLOSED on connect");
    }

    KT_CHECK_EQ(sock_close(rx), 0, "close receiver");
    KT_CHECK_EQ(sock_close(tx), 0, "close sender");
    sock_close(st);

    KT_RESULT("socket API");
}

/* ---- TCP foundation sanity (folded into the run, not a required marker) --- */
static void test_tcp_foundation(void) {
    KT_BEGIN();
    tcp_init();
    struct tcp_tcb *t = tcp_tcb_alloc();
    KT_CHECK(t != NULL, "tcb allocated");
    if (t) {
        KT_CHECK_EQ(t->state, TCP_CLOSED, "tcb starts CLOSED");
        tcp_listen(t, IP_ADDR_ANY, 8080);
        KT_CHECK_EQ(t->state, TCP_LISTEN, "listen -> LISTEN");

        /* Build a SYN segment and verify the pseudo-header checksum is valid. */
        struct ip_addr a = ip_make(10, 0, 0, 1);
        struct ip_addr b = ip_make(10, 0, 0, 2);
        struct netbuf *seg = tcp_build_segment(a, 1234, b, 80,
                                               1000, 0, TCP_F_SYN, 65535,
                                               NULL, 0);
        KT_CHECK(seg != NULL, "segment built");
        if (seg) {
            u16 c = net_transport_checksum(a.be, b.be, IP_PROTO_TCP,
                                           seg->data, (u16)netbuf_len(seg));
            KT_CHECK_EQ(c, 0, "tcp checksum valid");
            netbuf_free(seg);
        }
        tcp_tcb_free(t);
    }
    KT_RESULT("TCP foundation");
}

void net_tests_run(void) {
    kernel_log_line("[NET] running networking self-tests");

    test_nic();
    test_ethernet();
    test_arp();
    test_ipv4();
    test_icmp();
    test_udp();
    test_dhcp();
    test_dns();
    test_socket();
    test_tcp_foundation();

    kernel_log_ok("Networking foundation online");
}
