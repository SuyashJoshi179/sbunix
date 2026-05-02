#ifndef _ENDIAN_H
#define _ENDIAN_H

#include <stdint.h>
#include <byteswap.h>

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321
#define __PDP_ENDIAN    3412

/* SBUnix targets RISC-V — little-endian. */
#define __BYTE_ORDER __LITTLE_ENDIAN

#define LITTLE_ENDIAN  __LITTLE_ENDIAN
#define BIG_ENDIAN     __BIG_ENDIAN
#define PDP_ENDIAN     __PDP_ENDIAN
#define BYTE_ORDER     __BYTE_ORDER

#define htobe16(x) bswap_16(x)
#define htole16(x) ((uint16_t)(x))
#define be16toh(x) bswap_16(x)
#define le16toh(x) ((uint16_t)(x))

#define htobe32(x) bswap_32(x)
#define htole32(x) ((uint32_t)(x))
#define be32toh(x) bswap_32(x)
#define le32toh(x) ((uint32_t)(x))

#define htobe64(x) bswap_64(x)
#define htole64(x) ((uint64_t)(x))
#define be64toh(x) bswap_64(x)
#define le64toh(x) ((uint64_t)(x))

#endif
