#ifndef _BYTESWAP_H
#define _BYTESWAP_H

#include <stdint.h>

static inline uint16_t bswap_16(uint16_t x) {
    return (uint16_t)((x << 8) | (x >> 8));
}

static inline uint32_t bswap_32(uint32_t x) {
    return ((x & 0x000000FFu) << 24) |
           ((x & 0x0000FF00u) << 8)  |
           ((x & 0x00FF0000u) >> 8)  |
           ((x & 0xFF000000u) >> 24);
}

static inline uint64_t bswap_64(uint64_t x) {
    return ((uint64_t)bswap_32((uint32_t)x) << 32) |
           ((uint64_t)bswap_32((uint32_t)(x >> 32)));
}

#define __bswap_16(x) bswap_16(x)
#define __bswap_32(x) bswap_32(x)
#define __bswap_64(x) bswap_64(x)

#endif
