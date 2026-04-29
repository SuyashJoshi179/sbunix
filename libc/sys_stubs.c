#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* The long tail of POSIX file ops the kernel doesnt support. We supply
 * shape-correct stubs so portable code compiles; mutating ops report
 * success so installers/build systems don't bail out, while query ops
 * return the most permissive plausible answer. */

int chmod(const char *path, mode_t mode)        { (void)path; (void)mode; return 0; }
int fchmod(int fd, mode_t mode)                 { (void)fd;   (void)mode; return 0; }
mode_t umask(mode_t mask)                       { (void)mask; return 022; }
int mkfifo(const char *path, mode_t mode)       { (void)path; (void)mode; errno = ENOSYS; return -1; }
int mknod(const char *p, mode_t m, dev_t d)     { (void)p; (void)m; (void)d; errno = ENOSYS; return -1; }

/* stat/lstat: no SYS_stat in the kernel. Open the path RDONLY and call
 * fstat on the resulting fd. This works for regular files and dirs in
 * tarfs/sbfs but fails for anything that cant be opened. */
int stat(const char *path, struct stat *st) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int r = fstat(fd, st);
    close(fd);
    return r;
}
int lstat(const char *path, struct stat *st) { return stat(path, st); }

int creat(const char *path, mode_t mode) {
    (void)mode;
    return open(path, O_WRONLY | O_CREAT | O_TRUNC);
}

int openat(int dirfd, const char *path, int flags, ...) {
    (void)dirfd;  /* SBUnix has no openat — only AT_FDCWD makes sense */
    return open(path, flags);
}

/* fcntl: SBUnix has no F_GETLK/F_SETLK or per-fd flag storage. We service
 * F_DUPFD via dup, return success/zero for the descriptor-flag queries. */
int fcntl(int fd, int cmd, ...) {
    va_list ap; va_start(ap, cmd);
    int r = -1;
    switch (cmd) {
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
        r = dup(fd);
        break;
    case F_GETFD:
    case F_GETFL:
        r = 0;
        break;
    case F_SETFD:
    case F_SETFL:
        (void)va_arg(ap, int);
        r = 0;
        break;
    case F_GETLK:
    case F_SETLK:
    case F_SETLKW:
        errno = ENOSYS;
        r = -1;
        break;
    default:
        errno = EINVAL;
        r = -1;
    }
    va_end(ap);
    return r;
}
