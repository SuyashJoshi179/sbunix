#pragma once

/* Freestanding limits.h for the kernel.
 *
 * Reason this exists at all: GCC 15 ships `<gcc-include>/limits.h` as a
 * thin wrapper that does `#include_next <limits.h>` to chain to the C
 * library's real limits.h. Our build uses `-nostdinc` and adds only one
 * `-isystem` path (gcc's own include dir), so the `#include_next` chain
 * has nowhere to recurse to and the build fails:
 *
 *   limits.h:210: error: no include path in which to search for limits.h
 *
 * GCC 13's `include/limits.h` was self-contained and never triggered the
 * chain, which is why the same Makefile builds locally on GCC 13 but
 * fails on the GCC 15 prof-VM toolchain.
 *
 * Because `-Ikernel/include` is searched ahead of the gcc `-isystem`
 * path, dropping a freestanding limits.h here makes `#include <limits.h>`
 * resolve to this file. GCC's wrapper is never reached, no `#include_next`
 * chain is triggered, and the build works on any GCC version.
 *
 * Values mirror libc/include/limits.h so kernel and userspace agree. */

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
#define SSIZE_MAX    LONG_MAX
