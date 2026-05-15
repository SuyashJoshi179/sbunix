/*
 * POSIX conformance test: <poll.h>
 *
 * Reference: docs/susv5-html/basedefs/poll.h.html
 *
 * Audited POSIX functions: poll
 * Excluded (non-POSIX in SUSv5): ppoll (Linux extension)
 *
 * Required struct pollfd: fd (int), events (short), revents (short)
 * Required typedef: nfds_t
 * Required macros: POLLIN, POLLPRI, POLLOUT, POLLRDNORM, POLLRDBAND,
 *                  POLLWRNORM, POLLWRBAND, POLLERR, POLLHUP, POLLNVAL
 */
#include <poll.h>

#define PIN __attribute__((unused)) static

PIN int (*_pin_poll)(struct pollfd *, nfds_t, int) = poll;

__attribute__((unused))
static void _struct_fields(void) {
    struct pollfd p;
    __builtin_memset(&p, 0, sizeof p);
    (void)p.fd; (void)p.events; (void)p.revents;
}

__attribute__((unused))
static void _macro_checks(void) {
    int x = 0;
    x |= POLLIN; x |= POLLPRI; x |= POLLOUT;
    x |= POLLRDNORM; x |= POLLRDBAND; x |= POLLWRNORM; x |= POLLWRBAND;
    x |= POLLERR; x |= POLLHUP; x |= POLLNVAL;
    (void)x;
}
