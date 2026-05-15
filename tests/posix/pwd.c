/*
 * POSIX conformance test: <pwd.h>
 * Reference: docs/susv5-html/basedefs/pwd.h.html
 * Audited: getpwuid, getpwnam, getpwuid_r, getpwnam_r, getpwent, setpwent, endpwent
 * Required struct passwd: pw_name, pw_uid, pw_gid, pw_dir, pw_shell
 * Excluded (non-POSIX): pw_passwd, pw_gecos (BSD historical, POSIX no longer mandates)
 */
#include <pwd.h>

#define PIN __attribute__((unused)) static

PIN struct passwd *(*_pin_getpwuid)(uid_t) = getpwuid;
PIN struct passwd *(*_pin_getpwnam)(const char *) = getpwnam;
PIN int            (*_pin_getpwuid_r)(uid_t, struct passwd *, char *, size_t, struct passwd **) = getpwuid_r;
PIN int            (*_pin_getpwnam_r)(const char *, struct passwd *, char *, size_t, struct passwd **) = getpwnam_r;
PIN struct passwd *(*_pin_getpwent)(void) = getpwent;
PIN void           (*_pin_setpwent)(void) = setpwent;
PIN void           (*_pin_endpwent)(void) = endpwent;

__attribute__((unused))
static void _struct_fields(void) {
    struct passwd p;
    __builtin_memset(&p, 0, sizeof p);
    (void)p.pw_name; (void)p.pw_uid; (void)p.pw_gid;
    (void)p.pw_dir;  (void)p.pw_shell;
}
