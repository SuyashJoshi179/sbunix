/*
 * POSIX conformance test: <grp.h>
 * Reference: docs/susv5-html/basedefs/grp.h.html
 * Audited: getgrgid, getgrnam, getgrgid_r, getgrnam_r, getgrent, setgrent, endgrent
 * Required struct group: gr_name, gr_gid, gr_mem
 * Excluded (non-POSIX): gr_passwd (BSD historical), getgrouplist, getgroups (in unistd)
 */
#include <grp.h>

#define PIN __attribute__((unused)) static

PIN struct group *(*_pin_getgrgid)(gid_t) = getgrgid;
PIN struct group *(*_pin_getgrnam)(const char *) = getgrnam;
PIN int           (*_pin_getgrgid_r)(gid_t, struct group *, char *, size_t, struct group **) = getgrgid_r;
PIN int           (*_pin_getgrnam_r)(const char *, struct group *, char *, size_t, struct group **) = getgrnam_r;
PIN struct group *(*_pin_getgrent)(void) = getgrent;
PIN void          (*_pin_setgrent)(void) = setgrent;
PIN void          (*_pin_endgrent)(void) = endgrent;

__attribute__((unused))
static void _struct_fields(void) {
    struct group g;
    __builtin_memset(&g, 0, sizeof g);
    (void)g.gr_name; (void)g.gr_gid; (void)g.gr_mem;
}
