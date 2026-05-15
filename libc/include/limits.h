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

/* POSIX implementation-defined runtime limits.
 * Values are chosen at or above the POSIX-2017 minimums; portable code
 * uses these as compile-time upper bounds. */
#define PATH_MAX        4096
#define NAME_MAX        255
#define ARG_MAX         4096
#define OPEN_MAX        64
#define IOV_MAX         16
#define PIPE_BUF        4096
#define SSIZE_MAX       LONG_MAX
#define HOST_NAME_MAX   255
#define LOGIN_NAME_MAX  256
#define TTY_NAME_MAX    32
#define LINE_MAX        2048
#define CHILD_MAX       128
#define NGROUPS_MAX     16
#define PAGESIZE        4096
#define PAGE_SIZE       PAGESIZE
#define EXPR_NEST_MAX   32
#define RE_DUP_MAX      255
#define COLL_WEIGHTS_MAX 2
#define DELAYTIMER_MAX  32
#define MQ_OPEN_MAX     8
#define MQ_PRIO_MAX     32
#define SEM_NSEMS_MAX   256
#define SEM_VALUE_MAX   32767
#define SIGQUEUE_MAX    32
#define TIMER_MAX       32

/* POSIX-mandated minimum values that portable code can rely on without
 * inspecting our implementation values. */
#define _POSIX_PATH_MAX        256
#define _POSIX_NAME_MAX        14
#define _POSIX_ARG_MAX         4096
#define _POSIX_OPEN_MAX        20
#define _POSIX_PIPE_BUF        512
#define _POSIX_CHILD_MAX       25
#define _POSIX_HOST_NAME_MAX   255
#define _POSIX_LINK_MAX        8
#define _POSIX_LOGIN_NAME_MAX  9
#define _POSIX_MAX_CANON       255
#define _POSIX_MAX_INPUT       255
#define _POSIX_NGROUPS_MAX     8
#define _POSIX_RE_DUP_MAX      255
#define _POSIX_SSIZE_MAX       32767
#define _POSIX_STREAM_MAX      8
#define _POSIX_SYMLINK_MAX     255
#define _POSIX_SYMLOOP_MAX     8
#define _POSIX_TTY_NAME_MAX    9
#define _POSIX_TZNAME_MAX      6
#define _POSIX_DELAYTIMER_MAX  32
#define _POSIX_MQ_OPEN_MAX     8
#define _POSIX_MQ_PRIO_MAX     32
#define _POSIX_SEM_NSEMS_MAX   256
#define _POSIX_SEM_VALUE_MAX   32767
#define _POSIX_SIGQUEUE_MAX    32
#define _POSIX_TIMER_MAX       32
