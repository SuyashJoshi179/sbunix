#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>
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

/* pread/pwrite: POSIX-2001. Kernel has no SYS_pread/SYS_pwrite, so we
 * emulate via lseek+read+lseek. NOT atomic — concurrent file ops on the
 * same fd would race. SBUnix has single-threaded user processes so this
 * is safe enough; the failure mode is "we briefly move the file offset
 * while another caller from the same fd is mid-syscall," which already
 * never happens with our single-threaded model. */
static ssize_t _p_io(int fd, void *buf, size_t n, off_t off, int is_write,
                     const void *src) {
    off_t old = lseek(fd, 0, SEEK_CUR);
    if (old < 0) return -1;
    if (lseek(fd, off, SEEK_SET) < 0) return -1;
    ssize_t r = is_write ? write(fd, src, n) : read(fd, buf, n);
    int saved = errno;
    lseek(fd, old, SEEK_SET);
    errno = saved;
    return r;
}

ssize_t pread(int fd, void *buf, size_t n, off_t off) {
    return _p_io(fd, buf, n, off, 0, NULL);
}

ssize_t pwrite(int fd, const void *buf, size_t n, off_t off) {
    return _p_io(fd, NULL, n, off, 1, buf);
}

/* getlogin / getlogin_r: POSIX. SBUnix has no login database; report a
 * fixed name. getlogin returns a static buffer per POSIX. */
static char _login_name[] = "root";
char *getlogin(void) { return _login_name; }
int getlogin_r(char *buf, size_t len) {
    if (!buf || len == 0) { errno = ERANGE; return ERANGE; }
    size_t need = sizeof _login_name; /* incl. NUL */
    if (len < need) { errno = ERANGE; return ERANGE; }
    for (size_t i = 0; i < need; i++) buf[i] = _login_name[i];
    return 0;
}

/* posix_memalign: POSIX-2001 aligned allocator. alignment must be a
 * power of two and a multiple of sizeof(void *). We over-allocate by
 * (alignment - 1 + sizeof(void *)) and stash the original malloc pointer
 * just before the returned address so free() works. */
int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (!memptr) return EINVAL;
    if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0)
        return EINVAL;
    void *raw = malloc(size + alignment - 1 + sizeof(void *));
    if (!raw) return ENOMEM;
    uintptr_t base = (uintptr_t)raw + sizeof(void *);
    uintptr_t aligned = (base + alignment - 1) & ~(uintptr_t)(alignment - 1);
    ((void **)aligned)[-1] = raw;
    *memptr = (void *)aligned;
    return 0;
}

/* ---- *at family wrappers ----
 * The kernel has no dirfd-relative path resolution. Each wrapper checks
 * for dirfd==AT_FDCWD and delegates to the cwd-relative form; any other
 * dirfd is rejected with ENOSYS. Portable code that uses AT_FDCWD
 * exclusively works unchanged. */

int faccessat(int dirfd, const char *path, int mode, int flag) {
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    (void)flag;
    return access(path, mode);
}

int fchdir(int fd) {
    (void)fd;
    errno = ENOSYS;
    return -1;
}

int fchownat(int dirfd, const char *path, uid_t uid, gid_t gid, int flag) {
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    (void)flag;
    return chown(path, uid, gid);
}

int linkat(int olddirfd, const char *oldpath,
           int newdirfd, const char *newpath, int flags) {
    if (olddirfd != AT_FDCWD || newdirfd != AT_FDCWD) {
        errno = ENOSYS; return -1;
    }
    (void)flags;
    return link(oldpath, newpath);
}

int unlinkat(int dirfd, const char *path, int flag) {
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    if (flag & AT_REMOVEDIR) return rmdir(path);
    return unlink(path);
}

int symlinkat(const char *target, int newdirfd, const char *linkpath) {
    if (newdirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    return symlink(target, linkpath);
}

ssize_t readlinkat(int dirfd, const char *path, char *buf, size_t n) {
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    return readlink(path, buf, n);
}

int fstatat(int dirfd, const char *path, struct stat *buf, int flag) {
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    if (flag & AT_SYMLINK_NOFOLLOW) return lstat(path, buf);
    return stat(path, buf);
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flag) {
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    (void)flag;
    return chmod(path, mode);
}

int mkdirat(int dirfd, const char *path, mode_t mode) {
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    return mkdir(path, (int)mode);
}

int mkfifoat(int dirfd, const char *path, mode_t mode) {
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    return mkfifo(path, mode);
}

int mknodat(int dirfd, const char *path, mode_t mode, dev_t dev) {
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    return mknod(path, mode, dev);
}

int renameat(int olddirfd, const char *oldpath,
             int newdirfd, const char *newpath) {
    if (olddirfd != AT_FDCWD || newdirfd != AT_FDCWD) {
        errno = ENOSYS; return -1;
    }
    return rename(oldpath, newpath);
}

/* utimensat/futimens: POSIX inode-time setters. Our kernel has no syscall
 * to write atim/mtim; stub gracefully so portable build systems that
 * touch -d files don't fail catastrophically. Returns success because
 * many test suites tolerate "time didn't actually change" but bail on -1. */
int utimensat(int dirfd, const char *path,
              const struct timespec times[2], int flag) {
    (void)dirfd; (void)path; (void)times; (void)flag;
    return 0;
}

int futimens(int fd, const struct timespec times[2]) {
    (void)fd; (void)times;
    return 0;
}

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
