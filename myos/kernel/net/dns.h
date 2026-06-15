/* DNS query/response builder + parser (A records), resolver over UDP. */
#ifndef MYOS_NET_DNS_H
#define MYOS_NET_DNS_H

#include "types.h"
#include "net_addr.h"

#define DNS_PORT       53u
#define DNS_MAX_NAME   255u
#define DNS_MAX_PACKET 512u

/* Fixed 12-byte header. */
struct dns_header {
    u16 id;
    u16 flags;
    u16 qdcount;   /* questions      */
    u16 ancount;   /* answers        */
    u16 nscount;   /* authority      */
    u16 arcount;   /* additional     */
} __attribute__((packed));

/* Header flag bits (host order). */
#define DNS_FLAG_QR     0x8000u   /* response */
#define DNS_FLAG_AA     0x0400u
#define DNS_FLAG_TC     0x0200u
#define DNS_FLAG_RD     0x0100u   /* recursion desired */
#define DNS_FLAG_RA     0x0080u
#define DNS_RCODE_MASK  0x000Fu

/* Record types / classes. */
#define DNS_TYPE_A      1u
#define DNS_TYPE_NS     2u
#define DNS_TYPE_CNAME  5u
#define DNS_CLASS_IN    1u

/* Encode a query for `name`/`qtype` into `buf` (capacity `cap`). Returns the
 * encoded length, or 0 on error. */
size_t dns_build_query(u16 id, const char *name, u16 qtype,
                       u8 *buf, size_t cap);

/* Parse a DNS response in `buf`/`len`. On finding an A record, writes the
 * address to *out and returns 1. Returns 0 if no A record / parse error.
 * `match_id` (if non-zero) must equal the response id. */
int dns_parse_response(const u8 *buf, size_t len, u16 match_id,
                       struct ip_addr *out);

/* Encode a name as a DNS label sequence into buf; returns bytes written or 0. */
size_t dns_encode_name(const char *name, u8 *buf, size_t cap);

/* Resolve `name` to an IPv4 address by querying `server` over UDP. The query is
 * sent and the binding registered; resolution completes asynchronously when the
 * response arrives (dns_input). Returns 0 if the query was sent, -1 otherwise.
 * The resolved address (when ready) is placed in *out and *done set to 1. */
int dns_resolve(struct ip_addr server, const char *name,
                struct ip_addr *out, volatile int *done);

void dns_init(void);

#endif /* MYOS_NET_DNS_H */
