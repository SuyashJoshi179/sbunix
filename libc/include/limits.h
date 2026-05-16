#pragma once

#define CHAR_BIT     8

#define SCHAR_MIN    (-128)
#define SCHAR_MAX    127
#define UCHAR_MAX    255

/* char is signed on RISC-V GCC by default. */
#define CHAR_MIN     SCHAR_MIN
#define CHAR_MAX     SCHAR_MAX

#define SHRT_MIN     (-32768)
#define SHRT_MAX     32767
#define USHRT_MAX    65535

#define INT_MIN      (-2147483647 - 1)
#define INT_MAX      2147483647
#define UINT_MAX     4294967295U

/* lp64: long and long long are both 64-bit. */
#define LONG_MIN     (-9223372036854775807L - 1)
#define LONG_MAX     9223372036854775807L
#define ULONG_MAX    18446744073709551615UL

#define LLONG_MIN    (-9223372036854775807LL - 1)
#define LLONG_MAX    9223372036854775807LL
#define ULLONG_MAX   18446744073709551615ULL

#define MB_LEN_MAX   1

#define PATH_MAX     4096
#define NAME_MAX     255
#define ARG_MAX      4096
#define OPEN_MAX     64
#define IOV_MAX      16
#define PIPE_BUF     4096
#define LINK_MAX     127
#define SSIZE_MAX    LONG_MAX
