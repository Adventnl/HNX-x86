/* Host/network byte-order helpers.
 *
 * The kernel targets little-endian x86-64, so network (big-endian) conversions
 * are byte swaps. Provided as static inlines so every protocol module can use
 * them without a separate translation unit.
 */
#ifndef MYOS_NET_ENDIAN_H
#define MYOS_NET_ENDIAN_H

#include "types.h"

static inline u16 net_bswap16(u16 v) {
    return (u16)((v << 8) | (v >> 8));
}

static inline u32 net_bswap32(u32 v) {
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8)  |
           ((v & 0x00FF0000u) >> 8)  |
           ((v & 0xFF000000u) >> 24);
}

/* x86-64 is little-endian: host<->network is a swap. */
static inline u16 net_htons(u16 v) { return net_bswap16(v); }
static inline u16 net_ntohs(u16 v) { return net_bswap16(v); }
static inline u32 net_htonl(u32 v) { return net_bswap32(v); }
static inline u32 net_ntohl(u32 v) { return net_bswap32(v); }

/* Unaligned big-endian load/store helpers (packets are not aligned). */
static inline u16 net_get_be16(const void *p) {
    const u8 *b = (const u8 *)p;
    return (u16)(((u16)b[0] << 8) | b[1]);
}
static inline u32 net_get_be32(const void *p) {
    const u8 *b = (const u8 *)p;
    return ((u32)b[0] << 24) | ((u32)b[1] << 16) |
           ((u32)b[2] << 8)  | (u32)b[3];
}
static inline void net_put_be16(void *p, u16 v) {
    u8 *b = (u8 *)p;
    b[0] = (u8)(v >> 8);
    b[1] = (u8)v;
}
static inline void net_put_be32(void *p, u32 v) {
    u8 *b = (u8 *)p;
    b[0] = (u8)(v >> 24);
    b[1] = (u8)(v >> 16);
    b[2] = (u8)(v >> 8);
    b[3] = (u8)v;
}

#endif /* MYOS_NET_ENDIAN_H */
