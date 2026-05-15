#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* The long tail of POSIX file ops the kernel doesnt support. We supply
 * shape-correct stubs so portable code compiles; mutating ops report
 * success so installers/build systems don't bail out, while query ops
 * return the most permissive plausible answer. */

int chmod(const char *path, mode_t mode)        { (void)path; (void)mode; return 0; }
int fchmod(int fd, mode_t mode)                 { (void)fd;   (void)mode; return 0; }

/* Track umask in libc so install-style code that saves/restores via
 * `old = umask(0); ...; umask(old);` round-trips correctly. The kernel
 * has no permission bits to honor, so this is purely cosmetic state. */
static mode_t current_umask = 022;
mode_t umask(mode_t mask) {
    mode_t old = current_umask;
    current_umask = mask & 0777;
    return old;
}
int mkfifo(const char *path, mode_t mode)       { (void)path; (void)mode; errno = ENOSYS; return -1; }
int mknod(const char *p, mode_t m, dev_t d)     { (void)p; (void)m; (void)d; errno = ENOSYS; return -1; }

/* stat: no SYS_stat in the kernel. Open the path RDONLY and call
 * fstat on the resulting fd. This works for regular files and dirs in
 * tarfs/sbfs but fails for anything that can't be opened.
 *
 * NB: lstat() is a real syscall — see libc/syscall.c. Don't add a
 * duplicate definition here. */
int stat(const char *path, struct stat *st) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int r = fstat(fd, st);
    close(fd);
    return r;
}

int creat(const char *path, mode_t mode) {
    (void)mode;
    return open(path, O_WRONLY | O_CREAT | O_TRUNC);
}

int openat(int dirfd, const char *path, int flags, ...) {
    /* No real openat in the kernel. Accept only AT_FDCWD; reject any
     * other dirfd rather than silently opening the wrong path. */
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    (void)flags;
    return open(path, flags);
}

/* Duplicate fd to the lowest free descriptor >= minfd. Loops dup() and
 * closes intermediates so we honor the F_DUPFD/F_DUPFD_CLOEXEC contract
 * even though the kernel has no fcntl-aware allocator. The held buffer
 * is sized to minfd so the loop can occupy every descriptor below minfd
 * without spuriously hitting EMFILE. */
static int dup_to_minfd(int fd, int minfd) {
    if (minfd < 0) { errno = EINVAL; return -1; }
    int *held = NULL;
    if (minfd > 0) {
        held = malloc((size_t)minfd * sizeof(int));
        if (!held) { errno = ENOMEM; return -1; }
    }
    int nheld = 0;
    int out = -1;
    while (nheld < minfd) {
        int n = dup(fd);
        if (n < 0) break;
        if (n >= minfd) { out = n; break; }
        held[nheld++] = n;
    }
    for (int i = 0; i < nheld; i++) close(held[i]);
    free(held);
    return out;
}

/* fcntl: SBUnix has no F_GETLK/F_SETLK or per-fd flag storage. We service
 * F_DUPFD via dup, return success/zero for the descriptor-flag queries. */
int fcntl(int fd, int cmd, ...) {
    va_list ap; va_start(ap, cmd);
    int r = -1;
    switch (cmd) {
    case F_DUPFD:
    case F_DUPFD_CLOEXEC: {
        int minfd = va_arg(ap, int);
        r = dup_to_minfd(fd, minfd);
        break;
    }
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

/* ---- Tier-1 no-op stubs ----
 * POSIX requires these in our libc surface but the kernel does not back
 * them. Returning success (or a permissive value) lets portable code link
 * and run without exercising the missing feature. */

/* nice: process priority. Kernel scheduler is fixed round-robin; ignore. */
int nice(int incr) { (void)incr; return 0; }

/* lockf: advisory file lock. No file-lock subsystem in kernel. */
int lockf(int fd, int cmd, off_t len) { (void)fd; (void)cmd; (void)len; errno = ENOSYS; return -1; }

/* swab: copy n bytes from src to dst, swapping adjacent bytes per pair.
 * n is intentionally signed; POSIX says negative n is a no-op. */
void swab(const void *src, void *dst, ssize_t n) {
    if (n <= 0) return;
    const unsigned char *s = src;
    unsigned char *d = dst;
    for (ssize_t i = 0; i + 1 < n; i += 2) {
        d[i]   = s[i + 1];
        d[i+1] = s[i];
    }
}

/* confstr: configuration strings. Return 0 (no string available). */
size_t confstr(int name, char *buf, size_t len) {
    (void)name; (void)buf; (void)len; return 0;
}

/* posix_madvise: memory advice. Kernel ignores hints; always succeeds. */
int posix_madvise(void *addr, size_t len, int advice) {
    (void)addr; (void)len; (void)advice; return 0;
}

/* Memory locking: kernel does not swap, so all pages are de-facto locked. */
int mlock(const void *addr, size_t len)   { (void)addr; (void)len; return 0; }
int munlock(const void *addr, size_t len) { (void)addr; (void)len; return 0; }
int mlockall(int flags)   { (void)flags; return 0; }
int munlockall(void)      { return 0; }

/* Extended uid/gid setters. Kernel stores only a single uid/gid pair;
 * setting real/effective/saved to the same value is the natural behavior. */
int setregid(gid_t rgid, gid_t egid)              { (void)rgid; return setgid(egid); }
int setreuid(uid_t ruid, uid_t euid)              { (void)ruid; return setuid(euid); }
int setresgid(gid_t rgid, gid_t egid, gid_t sgid) { (void)rgid; (void)sgid; return setgid(egid); }
int setresuid(uid_t ruid, uid_t euid, uid_t suid) { (void)ruid; (void)suid; return setuid(euid); }
int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid) {
    gid_t g = getgid();
    if (rgid) *rgid = g;
    if (egid) *egid = g;
    if (sgid) *sgid = g;
    return 0;
}
int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid) {
    uid_t u = getuid();
    if (ruid) *ruid = u;
    if (euid) *euid = u;
    if (suid) *suid = u;
    return 0;
}

/* sched_setparam / sched_getparam / sched_rr_get_interval: kernel uses a
 * fixed round-robin scheduler with no priority. Stub gracefully. */
struct sched_param;
int sched_setparam(pid_t pid, const struct sched_param *param) {
    (void)pid; (void)param; return 0;
}
int sched_getparam(pid_t pid, struct sched_param *param) {
    (void)pid;
    /* sched_param's first member is int sched_priority; zero it without
     * pulling <sched.h> into this file (avoids type-decl ordering churn). */
    if (param) *(int *)param = 0;
    return 0;
}
struct timespec;
int sched_rr_get_interval(pid_t pid, struct timespec *ts) {
    (void)pid;
    if (ts) {
        /* Report ~10ms (matches kernel timer tick HZ=100). */
        long *p = (long *)ts;   /* {tv_sec, tv_nsec} */
        p[0] = 0;
        p[1] = 10000000L;
    }
    return 0;
}
