/*
 * POSIX conformance test: <sys/utsname.h>
 *
 * Reference: docs/susv5-html/basedefs/sys_utsname.h.html
 *
 * Audited POSIX functions: uname
 * Required struct utsname members: sysname, nodename, release, version, machine
 * (all char arrays; sizes implementation-defined).
 *
 * Our libc adds `domainname` (Linux extension); pin only POSIX-required fields.
 */
#include <sys/utsname.h>

#define PIN __attribute__((unused)) static

PIN int (*_pin_uname)(struct utsname *) = uname;

__attribute__((unused))
static void _struct_fields(void) {
    struct utsname u;
    __builtin_memset(&u, 0, sizeof u);
    (void)u.sysname[0]; (void)u.nodename[0]; (void)u.release[0];
    (void)u.version[0]; (void)u.machine[0];
}
