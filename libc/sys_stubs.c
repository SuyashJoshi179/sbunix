#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
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

/* SBUnix has no on-disk permission bits, so chmod/fchmod cannot actually
 * change anything. Validate the target exists (path lookup / fd
 * validity) before returning success so callers can still distinguish
 * "chmod a missing file" from "chmod a real file we don't enforce on". */
int chmod(const char *path, mode_t mode) {
    (void)mode;
    struct stat st;
    if (stat(path, &st) < 0) return -1;
    return 0;
}
int fchmod(int fd, mode_t mode) {
    (void)mode;
    struct stat st;
    if (fstat(fd, &st) < 0) return -1;
    return 0;
}

/* Track umask in libc so install-style code that saves/restores via
 * `old = umask(0); ...; umask(old);` round-trips correctly, and so a
 * libc-side caller of get_umask() can apply the mask manually before
 * a creation syscall. We intentionally don't propagate to the kernel:
 * SBUnix has no on-disk permission bits to enforce, so a kernel-side
 * umask register would be storage without observable effect. If/when
 * the kernel gains a permission system, both pieces should land
 * together (T3.12). */
static mode_t current_umask = 022;
mode_t umask(mode_t mask) {
    mode_t old = current_umask;
    current_umask = mask & 0777;
    return old;
}
mode_t __libc_get_umask(void) { return current_umask; }
/* No FIFO support in the kernel (no character-mode pipe with a name) and
 * no dynamic device-node creation — devfs is a hard-coded list. These
 * remain ENOSYS by design; truncate/ftruncate/symlink (T1.14, T1.15)
 * landed as real syscalls instead. (T3.20) */
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

/* fcntl: F_GETFD/F_SETFD, F_GETFL/F_SETFL are kernel-backed; the kernel
 * reconstructs F_GETFL from the per-file readable/writable/append fields.
 * F_DUPFD is serviced via dup; F_DUPFD_CLOEXEC additionally sets the
 * cloexec flag on the new descriptor. Advisory locks report ENOSYS. */
int fcntl(int fd, int cmd, ...) {
    va_list ap; va_start(ap, cmd);
    int r = -1;
    switch (cmd) {
    case F_DUPFD: {
        int minfd = va_arg(ap, int);
        r = dup_to_minfd(fd, minfd);
        break;
    }
    case F_DUPFD_CLOEXEC: {
        int minfd = va_arg(ap, int);
        r = dup_to_minfd(fd, minfd);
        if (r >= 0 && fcntl(r, F_SETFD, FD_CLOEXEC) < 0) {
            int saved = errno;
            close(r);
            errno = saved;
            r = -1;
        }
        break;
    }
    case F_GETFD:
    case F_GETFL:
        r = (int)syscall(SYS_fcntl, fd, cmd);
        break;
    case F_SETFD:
    case F_SETFL: {
        int arg = va_arg(ap, int);
        r = (int)syscall(SYS_fcntl, fd, cmd, arg);
        break;
    }
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
