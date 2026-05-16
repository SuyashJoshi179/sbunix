#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* fstatat / mkdirat / unlinkat: dirfd-relative lookup must hit the same
 * namei() path as plain stat/mkdir/unlink. AT_FDCWD must behave like the
 * non-*at variant. /tmp is tmpfs (writable); /bin is tarfs (read-only).
 *
 * The probe sequence mirrors what real installers do: open a working
 * directory once, then operate on relative names against it. */
int main(void) {
    /* AT_FDCWD path: fstatat(AT_FDCWD, "/bin/sh") must match stat. */
    struct stat st1, st2;
    if (stat("/bin/sh", &st1) < 0) {
        printf("FAIL: stat /bin/sh errno=%d\n", errno);
        return 1;
    }
    if (fstatat(AT_FDCWD, "/bin/sh", &st2, 0) < 0) {
        printf("FAIL: fstatat(AT_FDCWD,/bin/sh) errno=%d\n", errno);
        return 1;
    }
    if (!S_ISREG(st2.st_mode) || st2.st_size != st1.st_size) {
        printf("FAIL: fstatat AT_FDCWD mismatch mode=%o size=%ld\n",
               (unsigned)st2.st_mode, (long)st2.st_size);
        return 1;
    }

    /* Real dirfd: open /bin, fstatat(dirfd, "sh") must resolve relative. */
    int bfd = open("/bin", O_RDONLY);
    if (bfd < 0) {
        printf("FAIL: open /bin errno=%d\n", errno);
        return 1;
    }
    if (fstatat(bfd, "sh", &st2, 0) < 0) {
        printf("FAIL: fstatat(/bin,sh) errno=%d\n", errno);
        return 1;
    }
    if (!S_ISREG(st2.st_mode) || st2.st_size != st1.st_size) {
        printf("FAIL: fstatat dirfd mismatch\n");
        return 1;
    }
    close(bfd);

    /* ENOENT propagates. */
    errno = 0;
    if (fstatat(AT_FDCWD, "/no/such/path", &st2, 0) >= 0 || errno != ENOENT) {
        printf("FAIL: fstatat ENOENT: errno=%d\n", errno);
        return 1;
    }

    /* dirfd on a non-directory must report ENOTDIR. */
    int rfd = open("/bin/sh", O_RDONLY);
    if (rfd < 0) { printf("FAIL: open /bin/sh\n"); return 1; }
    errno = 0;
    if (fstatat(rfd, "x", &st2, 0) >= 0 || errno != ENOTDIR) {
        printf("FAIL: fstatat non-dir dirfd: errno=%d\n", errno);
        return 1;
    }
    close(rfd);

    /* mkdirat / unlinkat against tmpfs via a dirfd. */
    int tfd = open("/tmp", O_RDONLY);
    if (tfd < 0) { printf("FAIL: open /tmp errno=%d\n", errno); return 1; }

    /* Clean stale state from prior runs. */
    unlinkat(tfd, "atfam_d", 0);

    if (mkdirat(tfd, "atfam_d", 0755) < 0) {
        printf("FAIL: mkdirat(/tmp,atfam_d) errno=%d\n", errno);
        return 1;
    }
    if (fstatat(tfd, "atfam_d", &st2, 0) < 0 || !S_ISDIR(st2.st_mode)) {
        printf("FAIL: mkdirat produced no dir (mode=%o errno=%d)\n",
               (unsigned)st2.st_mode, errno);
        return 1;
    }
    if (unlinkat(tfd, "atfam_d", 0) < 0) {
        printf("FAIL: unlinkat(/tmp,atfam_d) errno=%d\n", errno);
        return 1;
    }
    if (fstatat(tfd, "atfam_d", &st2, 0) >= 0) {
        printf("FAIL: unlinkat left dir behind\n");
        return 1;
    }

    /* AT_FDCWD on mkdirat/unlinkat must work too. */
    if (mkdirat(AT_FDCWD, "/tmp/atfam_d2", 0755) < 0) {
        printf("FAIL: mkdirat AT_FDCWD errno=%d\n", errno);
        return 1;
    }
    if (unlinkat(AT_FDCWD, "/tmp/atfam_d2", 0) < 0) {
        printf("FAIL: unlinkat AT_FDCWD errno=%d\n", errno);
        return 1;
    }
    close(tfd);

    /* mkdirat into tarfs (read-only) must report EROFS, not silently succeed. */
    errno = 0;
    if (mkdirat(AT_FDCWD, "/bin/atfam_ro", 0755) >= 0 || errno != EROFS) {
        printf("FAIL: mkdirat tarfs: errno=%d (want EROFS=%d)\n", errno, EROFS);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
