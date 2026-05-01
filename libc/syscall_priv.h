#pragma once
#include <errno.h>

/* libc-internal helper: translate kernel return convention (negative
 * -errno on failure, non-negative on success) to POSIX (-1 with errno
 * set on failure, raw value on success). Pointer-returning wrappers
 * (mmap, sbrk) handle this inline because their valid range overlaps
 * with -errno encodings. */
static inline long syscall_ret(long r) {
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return r;
}
