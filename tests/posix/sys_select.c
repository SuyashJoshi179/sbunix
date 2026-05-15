/*
 * POSIX conformance test: <sys/select.h>
 *
 * Reference: docs/susv5-html/basedefs/sys_select.h.html
 *
 * Audited POSIX functions: select, pselect
 * Required macros: FD_CLR, FD_ISSET, FD_SET, FD_ZERO, FD_SETSIZE
 *
 * FD_* are macros in our libc, not functions (POSIX permits either form).
 * The _macro_checks function exercises each.
 */
#include <sys/select.h>

#define PIN __attribute__((unused)) static

PIN int (*_pin_select)(int, fd_set *, fd_set *, fd_set *, struct timeval *) = select;
PIN int (*_pin_pselect)(int, fd_set *, fd_set *, fd_set *,
                        const struct timespec *, const sigset_t *) = pselect;

__attribute__((unused))
static void _macro_checks(int fd) {
    fd_set s;
    FD_ZERO(&s);
    FD_SET(fd, &s);
    FD_CLR(fd, &s);
    int isset = FD_ISSET(fd, &s);
    (void)isset;
    (void)FD_SETSIZE;
}
