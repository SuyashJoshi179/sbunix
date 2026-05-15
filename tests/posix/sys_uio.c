/*
 * POSIX conformance test: <sys/uio.h>
 *
 * Reference: docs/susv5-html/basedefs/sys_uio.h.html
 *
 * Audited POSIX functions: writev, readv
 * Required struct iovec fields: iov_base (void *), iov_len (size_t)
 */
#include <sys/uio.h>

#define PIN __attribute__((unused)) static

PIN ssize_t (*_pin_readv)(int, const struct iovec *, int) = readv;
PIN ssize_t (*_pin_writev)(int, const struct iovec *, int) = writev;

__attribute__((unused))
static void _struct_fields(void) {
    struct iovec v;
    __builtin_memset(&v, 0, sizeof v);
    (void)v.iov_base; (void)v.iov_len;
}
