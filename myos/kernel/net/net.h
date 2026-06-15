/* MyOS networking subsystem master include + bring-up.
 *
 * net_init() initializes every layer (netbuf/eth/checksum, the interface
 * registry, ARP, IPv4 routing, ICMP, UDP, TCP, the socket table), brings up the
 * loopback interface, then probes the e1000 NIC. net_tests_run() exercises the
 * stack over loopback by injecting packets and validating parsing, checksums
 * and state machines.
 */
#ifndef MYOS_NET_H
#define MYOS_NET_H

#include "types.h"

/* Pull in the whole stack for consumers of the master header. */
#include "netbuf.h"
#include "net_endian.h"
#include "net_addr.h"
#include "ethernet.h"
#include "checksum.h"
#include "net_if.h"
#include "arp.h"
#include "ipv4.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "dhcp.h"
#include "dns.h"
#include "socket.h"
#include "e1000.h"

/* Initialize the networking subsystem and bring up loopback + probe e1000. */
void net_init(void);

/* Run the deterministic, self-contained network self-tests (loopback only). */
void net_tests_run(void);

#endif /* MYOS_NET_H */
