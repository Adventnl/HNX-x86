/* RFC1071 internet checksum implementation.
 *
 * Accumulates 16-bit big-endian words into a 32-bit sum, folds the carries,
 * then takes the one's complement. The accumulation is endianness-neutral:
 * pairs of bytes are summed as (hi<<8 | lo), so the folded result is already in
 * network byte order regardless of host endianness.
 */
#include "checksum.h"
#include "net_endian.h"

u32 net_csum_partial(const void *data, size_t len, u32 sum) {
    const u8 *p = (const u8 *)data;
    /* Sum 16-bit big-endian words. */
    while (len > 1) {
        sum += ((u32)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    /* Trailing odd byte is the high half of a 16-bit word. */
    if (len == 1) {
        sum += (u32)p[0] << 8;
    }
    return sum;
}

u16 net_csum_fold(u32 sum) {
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    u16 host = (u16)~sum;
    /* `sum` is accumulated in host-numeric form with big-endian word ordering,
     * so the folded 16-bit value must be byte-swapped to land in network order
     * in the header field. */
    return net_htons(host);
}

u16 net_checksum(const void *data, size_t len) {
    return net_csum_fold(net_csum_partial(data, len, 0));
}

u32 net_pseudo_header_sum(u32 src_be, u32 dst_be, u8 proto, u16 transport_len) {
    u32 sum = 0;
    /* Addresses are already network-order; feed them as raw bytes. */
    sum = net_csum_partial(&src_be, 4, sum);
    sum = net_csum_partial(&dst_be, 4, sum);
    /* zero | proto | length(16). */
    sum += (u32)proto;            /* high byte zero, low byte proto */
    sum += (u32)transport_len;    /* length in host order, as a 16-bit word */
    return sum;
}

u16 net_transport_checksum(u32 src_be, u32 dst_be, u8 proto,
                           const void *l4, u16 l4_len) {
    u32 sum = net_pseudo_header_sum(src_be, dst_be, proto, l4_len);
    sum = net_csum_partial(l4, l4_len, sum);
    return net_csum_fold(sum);
}
