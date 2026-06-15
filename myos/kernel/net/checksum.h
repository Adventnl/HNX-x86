/* RFC1071 internet checksum + transport pseudo-header helpers. */
#ifndef MYOS_NET_CHECKSUM_H
#define MYOS_NET_CHECKSUM_H

#include "types.h"

/* Raw 16-bit one's-complement sum accumulator. Feed chunks with
 * net_csum_partial(), then finalize with net_csum_fold(). `sum` carries the
 * running 32-bit accumulator across chunks. */
u32 net_csum_partial(const void *data, size_t len, u32 sum);

/* Fold a 32-bit accumulator into the final 16-bit one's-complement checksum
 * (network byte order, ready to drop into a header field). */
u16 net_csum_fold(u32 sum);

/* Convenience: full checksum over one contiguous buffer. */
u16 net_checksum(const void *data, size_t len);

/* IPv4 pseudo-header sum (for UDP/TCP). All addresses big-endian as on the
 * wire; proto is the IP protocol number; transport_len is the L4 length (header
 * + payload) in host order. Returns a partial sum to combine with the L4 bytes
 * via net_csum_partial() then net_csum_fold(). */
u32 net_pseudo_header_sum(u32 src_be, u32 dst_be, u8 proto, u16 transport_len);

/* Full transport checksum: pseudo-header + L4 segment. */
u16 net_transport_checksum(u32 src_be, u32 dst_be, u8 proto,
                           const void *l4, u16 l4_len);

#endif /* MYOS_NET_CHECKSUM_H */
