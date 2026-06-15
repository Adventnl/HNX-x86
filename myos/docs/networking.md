# Networking

The MyOS networking subsystem is a from-scratch IPv4/Ethernet stack living in
`kernel/net/`. It is a **foundation**: every layer from the packet buffer up to a
BSD-ish socket API is implemented, exercised end to end over a loopback
interface, and brought up at boot — but bulk TCP data transfer, retransmission
and a real PCI-attached NIC datapath are deliberately scoped as the documented
next steps. The whole subsystem is deterministic and self-contained: the
self-tests never touch a real network, so the verify target is reproducible.

Master header and bring-up entry point: `kernel/net/net.h` / `kernel/net/net.c`.
The kernel calls `net_init()` then `net_tests_run()` during boot
(`kernel/src/kernel.c:318-319`).

## Architecture

Packets flow through a classic layered stack built around a single
grow-down/consume-front packet buffer (`struct netbuf`). Each protocol module
owns one translation unit and one header, has an `*_init()` that resets its
global state, a build/output path and a parse/input path, and (where useful) a
set of statistics accessors used by the self-tests.

```
                 sock_create / sock_sendto / sock_recvfrom   (socket.c)
                                  |
        dhcp_input / dns_resolve  |  udp_bind callbacks
                       \          |          /
   icmp_send_echo --- icmp.c    udp.c     tcp.c --- tcp_connect/listen/close
              \         |         |         /
               +------ ipv4_output / ipv4_input ------+      (ipv4.c, routing)
                                  |
                          arp_resolve / arp_input            (arp.c, cache)
                                  |
                   eth_build / eth_parse  (ethernet.c)
                                  |
            netif_transmit  <->  netif_rx   (net_if.c, registry + loopback)
                                  |
                  netif->tx (loopback_tx | e1000_netif_tx)
                                  |
                        loopback  |  e1000 TX/RX rings (e1000.c)
```

**Transmit path (egress).** A protocol builds its L4 segment into a fresh
`netbuf` with headroom reserved, computes its checksum (over the IPv4
pseudo-header for UDP/TCP), and calls `ipv4_output()`. IPv4 resolves a route
(`route_lookup` longest-prefix, falling back to `netif_for_dest`/`netif_default`),
pushes the IPv4 header (`ipv4_build`), resolves the next hop's MAC via
`arp_resolve`, frames Ethernet (`eth_build`), and hands the framed buffer to
`netif_transmit`, which calls the driver's `tx` hook and updates statistics.

**Receive path (ingress).** A driver receives a frame into a `netbuf` and calls
`netif_rx`. That parses the Ethernet header (`eth_parse`), stamps `nb->netif`,
and dispatches by EtherType to `arp_input` or `ipv4_input`. `ipv4_input`
validates version/IHL/checksum, trims to `total_len`, strips the IP header and
dispatches by protocol to `icmp_input`, `udp_input` or `tcp_input`. UDP demuxes
to a bound receiver callback; the socket layer's callback queues the datagram.

**Loopback closes the loop.** The loopback interface's `tx` (`loopback_tx`)
clones the framed bytes into a fresh buffer and feeds them straight back through
`netif_rx`, so a packet sent to `127.0.0.1` is parsed and delivered by the real
receive path. This is how every protocol test runs without hardware.

## File map

| File | Role |
| --- | --- |
| `kernel/net/net.h` | Master include (pulls in the whole stack) + `net_init`/`net_tests_run` decls |
| `kernel/net/net.c` | `net_init()`: init every layer, bring up loopback, probe e1000, install routes |
| `kernel/net/netbuf.h` / `.c` | `struct netbuf` packet buffer: alloc/free, push/pull/put/trim/reserve |
| `kernel/net/net_endian.h` | host/network byte-order swaps + unaligned big-endian load/store helpers |
| `kernel/net/net_addr.h` | `struct ip_addr` (network-order u32) + constructors/predicates |
| `kernel/net/checksum.h` / `.c` | RFC1071 internet checksum + IPv4 pseudo-header / transport checksum |
| `kernel/net/ethernet.h` / `.c` | Ethernet II framing: `struct mac_addr`, `eth_build`/`eth_parse`, MAC predicates |
| `kernel/net/net_if.h` / `.c` | `struct netif`, interface registry (`NETIF_MAX=4`), `netif_rx`/`netif_transmit`, loopback |
| `kernel/net/arp.h` / `.c` | ARP header, 16-entry cache, request/reply state, `arp_resolve` |
| `kernel/net/ipv4.h` / `.c` | IPv4 header build/parse + checksum, routing table (`IP_ROUTE_MAX=8`), in/out |
| `kernel/net/icmp.h` / `.c` | ICMP echo request/reply (ping) + echo statistics counters |
| `kernel/net/udp.h` / `.c` | UDP header, 16-entry bind table, ephemeral ports, checksummed output, demux |
| `kernel/net/tcp.h` / `.c` | TCP header, 8-entry TCB table, RFC793 state machine, segment codec |
| `kernel/net/dhcp.h` / `.c` | DHCP client state machine (DISCOVER/OFFER/REQUEST/ACK) over UDP |
| `kernel/net/dns.h` / `.c` | DNS A-record query/response codec + async resolver over UDP |
| `kernel/net/socket.h` / `.c` | Socket table (`SOCKET_MAX=16`), BSD-ish API over UDP + TCB foundation |
| `kernel/net/e1000.h` / `.c` | Intel 82540EM (QEMU `e1000`) driver: PCI probe, MMIO, RX/TX rings |
| `kernel/tests/net_tests.c` | Deterministic loopback self-tests; emits the verify markers |

## Data structures

**`struct netbuf`** (`netbuf.h`). One contiguous backing region with four
pointers: `head` (start of allocation), `data` (start of live bytes), `tail`
(one past last live byte), `end` (one past allocation), plus `alloc` and stamped
layer pointers `eth`/`net`/`xport` and an owning `netif`. Headroom is
`data - head`; tailroom is `end - tail`. Default headroom is
`NETBUF_DEFAULT_HEADROOM = 64` (room for eth+ip+tcp/udp). Helpers are
inline (`netbuf_len`, `netbuf_headroom`, `netbuf_tailroom`). The backing buffer
and the struct are two separate `kmem` allocations (`netbuf_alloc_headroom`).

**`struct ip_addr`** (`net_addr.h`). A single `u32 be` in network byte order, so
it drops straight into headers and checksums. Built with `ip_make(a,b,c,d)` or
`ip_from_host(u32)`; compared with `ip_equal`; `ip_same_subnet(a,net,mask)`
masks both operands. Constants: `IP_ADDR_ANY`, `IP_ADDR_BROADCAST`,
`IP_ADDR_LOOPBACK` (127.0.0.1).

**`struct mac_addr`** (`ethernet.h`). Six octets. Globals `eth_broadcast`
(all-FF) and `eth_zero`. `mac_is_multicast` tests the group bit of octet 0.

**`struct netif`** (`net_if.h`). `name[8]`, `mac`, `ip`/`netmask`/`gateway`,
`mtu`, `flags` (`NETIF_FLAG_UP|LOOPBACK|BROADCAST`), the `tx` hook, opaque
`driver` pointer, a `struct netif_stats` (rx/tx packets/bytes/dropped/errors),
and `registered`. The registry is a fixed array `g_netifs[NETIF_MAX=4]`.

**`struct arp_entry`** (`arp.h`). `state` (`ARP_FREE`/`ARP_INCOMPLETE`/
`ARP_RESOLVED`), `ip`, `mac`, `age`. The cache is `g_arp_cache[ARP_CACHE_SIZE=16]`
with an LRU-by-`age` eviction (`arp_alloc_slot` picks a free slot or the oldest).

**`struct ip_route`** (`ipv4.h`). `dest`/`netmask`/`gateway`/`nif`, `valid`, and
`prefix_len` (precomputed by `mask_prefix_len`) for longest-prefix selection.
Table is `g_routes[IP_ROUTE_MAX=8]`.

**`struct udp_binding`** (`udp.h`). `local_ip` (0 = any), `local_port`, `recv`
callback + `ctx`. The lookup `udp_find` prefers an exact IP match over a wildcard
binding. Table size `UDP_BIND_MAX=16`. Ephemeral ports start at 49152.

**`struct tcp_tcb`** (`tcp.h`). The transmission control block: 4-tuple, the send
sequence space (`snd_una`/`snd_nxt`/`snd_wnd`/`iss`) and receive sequence space
(`rcv_nxt`/`rcv_wnd`/`irs`), and `state` (the 11 RFC793 states). Table is
`g_tcbs[TCP_TCB_MAX=8]`. `tcp_state_name()` renders a state for diagnostics.

**`struct dhcp_client`** (`dhcp.h`). `nif`, `state` (INIT/SELECTING/REQUESTING/
BOUND), transaction id `xid` (seeded `0x4D594F53` = "MYOS"), and the lease fields
extracted from options (`offered_ip`, `server_id`, `subnet_mask`, `router`,
`dns`, `lease_secs`).

**`struct socket`** (`socket.h`). `type` (`SOCK_DGRAM=1`/`SOCK_STREAM=2`), local
and remote endpoints, `bound`/`connected`, a datagram receive ring
(`rxq[SOCKET_RX_QUEUE=8]` of `struct sock_dgram`, each holding up to
`SOCKET_RX_PAYLOAD=1472` bytes) and a `tcb` pointer for stream sockets. Table is
`g_sockets[SOCKET_MAX=16]`; a descriptor is the array index.

**`struct e1000_device`** (`e1000.h`). Mapped MMIO base, `mac`, `has_eeprom`, the
RX/TX descriptor rings (`E1000_NUM_RX_DESC = E1000_NUM_TX_DESC = 32`) and their
per-slot 2 KiB buffers, ring cursors, an embedded `struct netif`, and
`initialized`. Legacy descriptor formats `struct e1000_rx_desc`/`e1000_tx_desc`
are packed to the hardware layout.

## Wire formats

All header structs are `__attribute__((packed))` and store multi-byte fields in
network byte order; readers use the unaligned `net_get_be16/32` helpers.

**Ethernet II (`struct eth_header`, 14 bytes).** `dst[6]`, `src[6]`,
`ethertype` (big-endian). `ETH_HLEN=14`, `ETH_MIN_LEN=60`, `ETH_MAX_DATA=1500`.
EtherTypes: `ETH_P_IPV4=0x0800`, `ETH_P_ARP=0x0806`, `ETH_P_IPV6=0x86DD`
(IPv6 is recognized as a constant but not handled).

**ARP (`struct arp_header`, 28 bytes).** `htype`/`ptype`/`hlen`/`plen`/`oper`
then `sha[6]`/`spa[4]`/`tha[6]`/`tpa[4]`. MyOS only accepts
`htype==ARP_HTYPE_ETHER(1)`, `ptype==ARP_PTYPE_IPV4(0x0800)`, `hlen==6`,
`plen==4`; opcodes `ARP_OP_REQUEST(1)` / `ARP_OP_REPLY(2)`.

**IPv4 (`struct ipv4_header`, 20 bytes, no options).** `ver_ihl`, `tos`,
`total_len`, `id`, `frag_off`, `ttl`, `proto`, `checksum`, `src`, `dst`.
`ipv4_version`/`ipv4_ihl`/`ipv4_hdr_bytes` are inline accessors. Output always
sets version 4, IHL 5, and `IPV4_FLAG_DF(0x4000)` in `frag_off` (no
fragmentation). Protocols: `IP_PROTO_ICMP=1`, `IP_PROTO_TCP=6`,
`IP_PROTO_UDP=17`.

**ICMP (`struct icmp_header`, 8 bytes).** `type`, `code`, `checksum`, `id`,
`seq`. Types handled: `ICMP_ECHO_REQUEST(8)` and `ICMP_ECHO_REPLY(0)`;
`ICMP_DEST_UNREACH(3)` / `ICMP_TIME_EXCEED(11)` are defined but not generated.

**UDP (`struct udp_header`, 8 bytes).** `src_port`, `dst_port`, `length`,
`checksum`. The checksum covers the IPv4 pseudo-header + the datagram; a computed
0 is sent as `0xFFFF`; a received 0 means "no checksum" and validation is skipped.

**TCP (`struct tcp_header`, 20 bytes, no options).** `src_port`, `dst_port`,
`seq`, `ack`, `data_off` (high nibble = header words), `flags`, `window`,
`checksum`, `urg_ptr`. Flags: `TCP_F_FIN/SYN/RST/PSH/ACK/URG`
(`0x01/0x02/0x04/0x08/0x10/0x20`). `tcp_data_offset_words`/`tcp_header_bytes`
are inline accessors.

**DHCP (`struct dhcp_message`, fixed part + 312-byte options).** BOOTP layout:
`op`/`htype`/`hlen`/`hops`, `xid`, `secs`/`flags`, `ciaddr`/`yiaddr`/`siaddr`/
`giaddr`, `chaddr[16]`, `sname[64]`, `file[128]`, `magic` (`0x63825363`), then
TLV `options[312]`. The encoded length is the fixed part plus the option bytes
written, not `sizeof` — `dhcp_build` returns
`sizeof - sizeof(options) + off`. Ports: server 67, client 68.

**DNS (`struct dns_header`, 12 bytes).** `id`, `flags`, `qdcount`, `ancount`,
`nscount`, `arcount`. Flag bits `DNS_FLAG_QR/AA/TC/RD/RA` and `DNS_RCODE_MASK`.
Names are label sequences (`dns_encode_name`); responses may use compression
pointers (`0xC0` prefix), which `dns_skip_name` follows by length only. Only A
records (`DNS_TYPE_A=1`, `DNS_CLASS_IN=1`) are extracted; `DNS_MAX_PACKET=512`.

## Packet-flow walkthroughs

**ICMP echo over loopback (the `[PASS] ICMP ping` path).**

1. `icmp_send_echo(127.0.0.1, id, seq, payload, len)` -> `icmp_build` allocates a
   netbuf with default headroom, puts the 8-byte ICMP header + payload, and
   checksums the whole message (`net_checksum(nb->data, len)`).
2. `ipv4_output(nb, ANY, 127.0.0.1, ICMP, 64)`: `route_lookup` matches the
   `127.0.0.0/8` route installed by `net_init`, source defaults to `lo`'s IP,
   `ipv4_build` pushes+checksums the IPv4 header, `arp_resolve` returns `lo`'s
   MAC immediately (loopback flag), `eth_build` pushes the Ethernet header,
   `netif_transmit` calls `loopback_tx`.
3. `loopback_tx` copies the framed bytes into a fresh netbuf and calls
   `netif_rx`, which `eth_parse`s and dispatches to `ipv4_input`.
4. `ipv4_input` validates and strips the IP header, dispatches to `icmp_input`,
   which sees an echo request, increments `g_echo_requests_seen`, and builds a
   reply via `icmp_build` -> `ipv4_output(reply, dst, src, ...)`.
5. The reply loops the same way; the second `icmp_input` sees an echo reply and
   increments `g_echo_replies_seen`. The test asserts all three counters +1.

**UDP send/receive (`[PASS] UDP` / socket path).** `udp_output` builds the header,
resolves a source address for the pseudo-header checksum if `src` is zero
(`route_lookup`/`netif_for_dest`/`netif_default`), checksums, and calls
`ipv4_output`. Inbound, `ipv4_input` -> `udp_input` validates length and
(optional) checksum, then `udp_find(dst, dst_port)` selects the binding (exact IP
preferred over wildcard) and invokes its `recv(ctx, src, sport, dst, dport,
payload, plen)`. For sockets the callback is `sock_udp_recv`, which enqueues into
the socket's `rxq` ring; `sock_recvfrom` drains it.

**TCP handshake (foundation).** `tcp_connect` picks an ISS (`tcp_gen_iss`, a
deterministic `+64000` generator), sets `snd_una=snd_nxt=iss`, moves to
`SYN_SENT`, and `tcp_send_segment(SYN)` (which bumps `snd_nxt` past the SYN).
Inbound segments reach `tcp_input` -> `tcp_lookup` (4-tuple, falling back to a
`LISTEN` TCB) -> `tcp_state_machine`, which implements the RFC793 transitions:
`LISTEN`+SYN -> SYN_RECEIVED (send SYN|ACK); `SYN_SENT`+SYN|ACK -> ESTABLISHED
(send ACK); `ESTABLISHED`+FIN -> CLOSE_WAIT; the FIN_WAIT/CLOSING/LAST_ACK/
TIME_WAIT close transitions. Data bytes advance `rcv_nxt` and trigger an ACK, but
are not buffered for the application.

## Key APIs

**netbuf** (`netbuf.h`)
- `netbuf_alloc(cap)` / `netbuf_alloc_headroom(cap, headroom)` — allocate.
- `netbuf_reserve(nb, len)` — reserve headroom on an empty buffer.
- `netbuf_push(nb, len)` — prepend header space, return new `data` (build path).
- `netbuf_pull(nb, len)` — consume from front, return new `data` (parse path).
- `netbuf_put(nb, len)` / `netbuf_put_data(nb, src, len)` — append at tail.
- `netbuf_trim(nb, len)` — shrink to `len` live bytes; `netbuf_reset` re-arm.

**byte order / checksum**
- `net_htons/ntohs/htonl/ntohl`, `net_get_be16/32`, `net_put_be16/32`.
- `net_checksum(data,len)` — full RFC1071 checksum (a valid header sums to 0).
- `net_csum_partial`/`net_csum_fold` — incremental accumulation + finalize.
- `net_transport_checksum(src,dst,proto,l4,l4_len)` — pseudo-header + segment.

**interface** (`net_if.h`)
- `netif_register/get/get_by_index/count/default/for_dest`.
- `netif_set_addr`, `netif_up`, `netif_down`, `netif_is_up`.
- `netif_transmit(nif, nb)` — driver tx + stats; `netif_rx(nif, nb)` — parse +
  dispatch, **consumes nb**.
- `loopback_init()` / `loopback_get()`.

**ethernet / ARP / IPv4 / ICMP**
- `eth_build(nb,dst,src,ethertype)` / `eth_parse(nb,&dst,&src,&et)`.
- `arp_resolve(nif,ip,&mac)` returns 0 (resolved), 1 (pending, request sent), -1.
- `arp_cache_insert/lookup`, `arp_send_request`, `arp_announce`, `arp_input`.
- `route_add(dest,mask,gw,nif)` / `route_lookup(dst,&next_hop)`.
- `ipv4_build` / `ipv4_output(nb,src,dst,proto,ttl)` / `ipv4_input`.
- `icmp_send_echo(dst,id,seq,payload,len)`; counters
  `icmp_echo_requests_seen/replies_sent/replies_seen`.

**UDP / TCP**
- `udp_bind(ip,port,recv,ctx)` / `udp_unbind` / `udp_ephemeral_port`.
- `udp_output(src,sport,dst,dport,data,len)` — checksummed, 0xFFFF-for-zero rule.
- `tcp_tcb_alloc/free`, `tcp_listen`, `tcp_connect`, `tcp_close`.
- `tcp_build_segment(...)`, `tcp_send_segment(tcb,flags,data,len)`, `tcp_lookup`.

**DHCP / DNS**
- `dhcp_client_init`, `dhcp_start`, `dhcp_build`, `dhcp_parse`, `dhcp_input`.
- `dns_build_query`, `dns_parse_response`, `dns_encode_name`, `dns_resolve`.

**socket** (`socket.h`)
- `sock_create(type)` -> descriptor; `sock_bind/connect/sendto/recvfrom/close`;
  `sock_get(sd)` accessor for tests.

**e1000** — `e1000_init` (PCI probe), `e1000_attach`, `e1000_bringup`,
`e1000_tx_raw`, `e1000_poll`, `e1000_primary`.

## Invariants

- **Packet bytes are big-endian on the wire.** `struct ip_addr.be` and all
  multi-byte header fields are stored network-order; readers use
  `net_get_be16/32` (unaligned-safe) because packet structs are not aligned.
- **A correct header/segment checksums to zero.** Both `ipv4_input` and
  `icmp_input`/`udp_input`/`tcp_input` validate by re-summing the whole region
  and requiring 0. UDP additionally substitutes `0xFFFF` for a computed zero
  checksum (per RFC768), and skips validation when the received checksum is 0.
- **netbuf ownership.** `netif_rx` consumes (frees indirectly through callers in
  most paths) the buffer it is handed; `netif_transmit` frees on the error path
  and the driver tx hook frees on success (`e1000_netif_tx` and `loopback_tx`
  manage their own copies). L4 `*_input` handlers do **not** free `nb`; the
  caller (`ipv4_input` <- `netif_rx`) owns it.
- **Headroom discipline.** Build paths reserve `NETBUF_DEFAULT_HEADROOM` so the
  Ethernet (14) + IPv4 (20) + L4 headers can be pushed without reallocation;
  `netbuf_push` returns NULL rather than growing the buffer.
- **ARP self-resolution.** On a loopback interface `arp_resolve` returns the
  interface MAC immediately; the limited broadcast and the directed broadcast of
  the local subnet resolve to `eth_broadcast` without a request.
- **Routing is longest-prefix.** `route_lookup` selects the valid route with the
  largest `prefix_len`; a default route is `netmask = 0` (prefix 0). On-link
  routes (`gateway == 0`) use the destination as next hop.
- **Loopback always exists.** `net_init` brings up `lo` (127.0.0.1/8) first and
  installs its route, so the local stack and the tests work with no NIC.
- **Sequence-number bookkeeping.** `tcp_send_segment` advances `snd_nxt` by 1 for
  a SYN, by 1 for a FIN, and by the payload length, matching RFC793 sequence
  consumption.

## e1000 driver detail

The driver targets the Intel 82540EM (`8086:100E`), QEMU's default `e1000`. The
bring-up sequence in `e1000_bringup` is the real hardware path, shared by the PCI
attach (`e1000_attach`/`e1000_init`) and the RAM-backed self-test:

1. **MMIO access** is through `e1000_read`/`e1000_write` on `volatile u32`
   pointers into the mapped BAR0 window (`e1000_attach` maps a 2 MiB MMIO window
   via `vmm_map_mmio_2m`).
2. **Reset** (`e1000_reset`): mask all interrupts (`IMC=0xFFFFFFFF`), set
   `CTRL.RST`, settle, then set `CTRL.SLU|ASDE` (link up + auto-speed) and mask
   interrupts again.
3. **EEPROM / MAC**: `e1000_detect_eeprom` probes `EERD`; `e1000_read_mac` reads
   the MAC from EEPROM words 0–2 if present, otherwise from `RAL0`/`RAH0`;
   `e1000_set_mac` programs `RAL0`/`RAH0` with the Address-Valid bit.
4. **Rings** (`e1000_setup_rx`/`e1000_setup_tx`): each ring is one
   `pmm_alloc_page()` of 32 legacy descriptors; each descriptor gets its own
   4 KiB page buffer. RX programs `RDBAL/RDBAH/RDLEN/RDH/RDT` and enables
   `RCTL.EN|BAM|SECRC` with 2048-byte buffers; TX programs `TDBAL/.../TDT`,
   enables `TCTL.EN|PSP`, and sets the recommended `TIPG`.
5. **netif registration**: an embedded `struct netif` (name `eth0`, MTU 1500,
   broadcast flag, `tx = e1000_netif_tx`, `driver = dev`) is registered.

**TX** (`e1000_tx_frame`): copy the frame into the current TX buffer, set the
descriptor `addr`/`length`/`cmd` (`EOP|IFCS|RS`), clear status, advance `tx_cur`,
and write the new `TDT`. If the slot's `DD` bit is not set the ring is treated as
full and the frame is dropped (`-1`). `e1000_netif_tx` frames-then-frees the
netbuf. **RX** (`e1000_poll`): walk descriptors whose `STAT_DD` is set, copy each
into a netbuf, `netif_rx` it, recycle the descriptor (clear status, write `RDT`),
and advance `rx_cur`. There is no IRQ wiring in production; `e1000_poll` would be
called from an IRQ handler or poll loop.

In QEMU with no network backend the link never comes up, so production traffic
stays on loopback; the structural bring-up (registers, rings, MAC, TX descriptor)
is what the `[OK] NIC driver` test validates against a RAM-backed register window.

## Memory and statistics

**Allocation.** netbufs are backed by the kernel allocator: the `struct netbuf`
is `kmem_zalloc`'d and the data region is a separate `kmem_alloc`; `netbuf_free`
frees both. e1000 ring and buffer pages come from the physical allocator
(`pmm_alloc_page`). Protocol tables (ARP cache, routes, UDP bindings, TCBs,
sockets) are fixed-size file-static arrays — no per-connection heap allocation.

**Statistics.** `struct netif_stats` accumulates rx/tx packets/bytes plus
dropped/error counters, updated in `netif_transmit` (tx) and `netif_rx` /
`ipv4_input` (rx). Protocol-level counters used by the tests:
`icmp_echo_requests_seen` / `icmp_echo_replies_sent` / `icmp_echo_replies_seen`
(ICMP), `udp_datagrams_received` (UDP), and `arp_cache_count` / `route_count` /
`netif_count` for table sizes.

## Failure modes

- **Allocation failure.** Any `netbuf_alloc*` returning NULL aborts the build and
  the caller returns -1; `ipv4_output` frees `nb` and returns -1 on any framing
  failure.
- **Pending / unresolvable ARP.** `ipv4_output` drops the packet (no transmit
  queue in the foundation): it increments `tx_dropped`, frees `nb`, and returns
  `1` (pending, request sent) or `-1` (hard error). The retry must come from a
  higher layer re-sending.
- **No route / no interface.** `ipv4_output` falls back to `netif_for_dest` then
  `netif_default`; if still NULL it frees `nb` and returns -1.
- **Bad inbound packet.** Short frames, wrong version/IHL, bad checksums, or
  `total_len`/`length` fields that don't fit the buffer are dropped and counted
  in `rx_errors`/`rx_dropped`; unknown EtherTypes/protocols are dropped.
- **Duplicate UDP bind.** `udp_bind` rejects an exact (ip,port) duplicate with -1
  (the wildcard vs specific distinction is preserved by `udp_find`).
- **Socket RX overflow.** `sock_udp_recv` drops a datagram when `rx_count`
  reaches `SOCKET_RX_QUEUE`; oversized payloads are truncated to
  `SOCKET_RX_PAYLOAD`.
- **TCP with no matching TCB.** `tcp_input` silently drops (a full stack would
  emit a RST — noted in the source).
- **No NIC.** `e1000_init` returns -1 when the PCI device (`8086:100E`) is
  absent; `net_init` logs "e1000 NIC not present (loopback-only)" and continues.
  In QEMU without a network backend the link never comes up, but ring/register
  bring-up is real.

## Verification

Build target (in `Makefile.production`): **`verify-network`**. It boots the real
image under QEMU and greps the serial log (`tools/verify_qemu.py --expect`) for
markers emitted by `net_tests_run()` in `kernel/tests/net_tests.c`. There are no
hardcoded pass strings — each marker is printed only by code that ran.

Required serial markers (all must appear):

```
[OK] NIC driver
[PASS] Ethernet
[PASS] ARP
[PASS] IPv4
[PASS] ICMP ping
[PASS] UDP
[PASS] DHCP
[PASS] DNS
[PASS] socket API
[OK] Networking foundation online
```

Per-protocol sub-targets grep the same network log for a single marker each:
`verify-ethernet`, `verify-arp`, `verify-ipv4`, `verify-icmp`, `verify-udp`,
`verify-dhcp`, `verify-dns`, `verify-sockets`.

What each test actually does (`kernel/tests/net_tests.c`):

- **`[OK] NIC driver`** (`nic_driver_test`): brings the e1000 up against a 64 KiB
  RAM-backed register window (no PCI, no device), seeds RAL0/RAH0, and checks the
  MAC read-back, ring construction (`RDLEN`/`TDLEN` programmed), and a real TX
  descriptor post (length, EOP command bit, `TDT` advance).
- **`[PASS] Ethernet`**: builds a frame around a payload, parses it back, checks
  MAC roundtrip + ethertype + residual payload length, and the MAC predicates.
- **`[PASS] ARP`**: injects an ARP reply and confirms the sender mapping is
  learned; tests manual insert/lookup and a miss.
- **`[PASS] IPv4`**: builds a header (version/IHL/total_len/proto/addresses), and
  checks `net_checksum == 0`; then installs `/8` and `/24` routes and confirms
  longest-prefix and gateway selection.
- **`[PASS] ICMP ping`**: pings `127.0.0.1` over loopback; the request loops to
  `icmp_input`, which builds a reply that loops back and is observed as a reply
  (requests_seen, replies_sent, replies_seen all +1).
- **`[PASS] UDP`**: binds `127.0.0.1:7777`, sends "hello-udp" over loopback,
  verifies delivery, payload, source port; checks duplicate-bind rejection.
- **`[PASS] DHCP`**: builds a DISCOVER (chaddr/magic/option order), parses a
  hand-built OFFER, drives OFFER->REQUESTING and ACK->BOUND, and confirms the
  lease commits onto the interface IP.
- **`[PASS] DNS`**: builds a query (first label length = 3 for "www"), builds a
  compressed A response, parses it, and confirms wrong-id rejection.
- **`[PASS] socket API`**: two `SOCK_DGRAM` sockets exchange a datagram over
  loopback (length/payload/source-port/source-ip), the empty queue returns -1,
  and a `SOCK_STREAM` connect leaves the TCB out of `CLOSED`.
- **`[OK] Networking foundation online`**: printed after all tests run, plus a
  folded TCP-foundation sanity test (`tcp_build_segment` checksum valid, LISTEN
  transition) that is not a required marker.

`net_init()` itself emits `[OK] Loopback interface online`, an `[OK] e1000 NIC
online` (only when a device is present), and `[OK] Networking subsystem online`.

## Future expansion

The source comments mark these as the explicit next milestones:

- **TCP data transfer.** The TCB tracks full send/recv sequence space and the
  handshake/close transitions run, but bulk data, retransmission, windowing and
  reassembly are not implemented; `tcp_input` drops segments with no TCB instead
  of sending RST. Stream sockets connect/close but do not queue data yet.
- **IP output queueing.** `ipv4_output` drops on pending ARP. A real driver path
  needs a per-neighbor pending-packet queue flushed on ARP reply.
- **Live NIC datapath.** The e1000 RX path (`e1000_poll`) and TX ring are real,
  but there is no IRQ wiring or poll loop driving `e1000_poll` in production, and
  the NIC is given a static address (`10.0.2.15/24`) rather than running DHCP.
- **Per-process descriptors.** Sockets live in one kernel-global table; a future
  per-process descriptor table can wrap these (noted in `socket.h`).
- **DNS concurrency.** `dns_resolve` keeps a single in-flight `g_resolution`; a
  resolver cache and multiple outstanding queries are the next step.
- **IPv6, fragmentation, options.** Only IPv4 with no options/fragmentation is
  handled (DF is always set on output; IHL > 5 is parsed but not produced).
