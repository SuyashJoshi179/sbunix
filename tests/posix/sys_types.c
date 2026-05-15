/*
 * POSIX conformance test: <sys/types.h>
 * Reference: docs/susv5-html/basedefs/sys_types.h.html
 *
 * Types-only header; no function pins. Validates that every POSIX typedef
 * our libc claims to expose is visible from this header.
 *
 * Required typedefs (only those our libc declares):
 *   pid_t, uid_t, gid_t, id_t, mode_t, off_t, size_t, ssize_t, time_t,
 *   clock_t, dev_t, ino_t, nlink_t, blkcnt_t, blksize_t, suseconds_t
 *
 * Excluded (POSIX, not in our libc):
 *   clockid_t, timer_t, useconds_t, fsblkcnt_t, fsfilcnt_t, key_t,
 *   pthread_*, trace_*
 */
#include <sys/types.h>

__attribute__((unused))
static void _type_checks(void) {
    pid_t       a = 0; (void)a;
    uid_t       b = 0; (void)b;
    gid_t       c = 0; (void)c;
    id_t        d = 0; (void)d;
    mode_t      e = 0; (void)e;
    off_t       f = 0; (void)f;
    size_t      g = 0; (void)g;
    ssize_t     h = 0; (void)h;
    time_t      i = 0; (void)i;
    clock_t     j = 0; (void)j;
    dev_t       k = 0; (void)k;
    ino_t       l = 0; (void)l;
    nlink_t     m = 0; (void)m;
    blkcnt_t    n = 0; (void)n;
    blksize_t   o = 0; (void)o;
    suseconds_t p = 0; (void)p;
}
