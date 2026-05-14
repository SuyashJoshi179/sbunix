/*
 * POSIX conformance test: <fcntl.h>
 *
 * Reference: docs/susv5-html/basedefs/fcntl.h.html
 *
 * Audited POSIX functions: fcntl, creat, open, openat
 * Required struct flock fields: l_type, l_whence, l_start, l_len, l_pid
 * Required macros: O_RDONLY..O_CLOEXEC, F_DUPFD..F_SETLKW, F_RDLCK..F_UNLCK,
 *                  AT_FDCWD, AT_SYMLINK_NOFOLLOW
 *
 * Excluded (not implemented): posix_fadvise, posix_fallocate
 */
#include <fcntl.h>

#define PIN __attribute__((unused)) static

PIN int (*_pin_fcntl)(int, int, ...) = fcntl;
PIN int (*_pin_creat)(const char *, mode_t) = creat;
PIN int (*_pin_open)(const char *, int, ...) = open;
PIN int (*_pin_openat)(int, const char *, int, ...) = openat;

__attribute__((unused))
static void _struct_fields(void) {
    struct flock fl;
    __builtin_memset(&fl, 0, sizeof fl);
    (void)fl.l_type; (void)fl.l_whence; (void)fl.l_start;
    (void)fl.l_len; (void)fl.l_pid;
}

__attribute__((unused))
static void _macro_checks(void) {
    int x = O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_NOCTTY
          | O_TRUNC | O_APPEND | O_NONBLOCK | O_CLOEXEC;
    x |= F_DUPFD | F_GETFD | F_SETFD | F_GETFL | F_SETFL
       | F_GETLK | F_SETLK | F_SETLKW;
    x |= F_RDLCK | F_WRLCK | F_UNLCK;
    x |= AT_FDCWD | AT_SYMLINK_NOFOLLOW;
    (void)x;
}
