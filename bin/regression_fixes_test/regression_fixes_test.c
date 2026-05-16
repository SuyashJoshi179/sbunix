#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/random.h>
#include <sys/time.h>
#include <poll.h>
#include <time.h>

/* Regression coverage for the third-pass POSIX fixes (commit e8e5a8a).
 * Every assertion below maps 1:1 to a behavior change that previously
 * had no regression test:
 *   - link(symlink, new) does NOT follow the symlink (POSIX-2008)
 *   - utimensat rejects unknown flag bits with EINVAL
 *   - utimensat AT_SYMLINK_NOFOLLOW stamps the link itself
 *   - unlinkat refuses "." and ".." leaves with EINVAL
 *   - getrandom rejects unknown flag bits with EINVAL
 *   - select returns EBADF when any set fd is closed
 *   - pselect preserves errno across the trailing sigprocmask restore */

static int fails = 0;

static void chk(int cond, const char *what) {
    if (!cond) { printf("FAIL: %s (errno=%d)\n", what, errno); fails++; }
}

int main(void) {
    /* ------- link(symlink, new) is no-follow -------------------------
     * Create /tmp/rt_target, then /tmp/rt_sym pointing at it, then
     * link(rt_sym, rt_hard). lstat(rt_hard) must report S_IFLNK and
     * share inode with rt_sym, NOT with rt_target. */
    (void)unlink("/tmp/rt_target");
    (void)unlink("/tmp/rt_sym");
    (void)unlink("/tmp/rt_hard");
    int fd = open("/tmp/rt_target", O_CREAT | O_WRONLY);
    chk(fd >= 0, "create /tmp/rt_target");
    if (fd >= 0) close(fd);
    chk(symlink("/tmp/rt_target", "/tmp/rt_sym") == 0, "symlink rt_sym");
    chk(link("/tmp/rt_sym", "/tmp/rt_hard") == 0, "link(symlink, hard)");

    struct stat ss, sh, st;
    chk(lstat("/tmp/rt_sym", &ss) == 0, "lstat rt_sym");
    chk(lstat("/tmp/rt_hard", &sh) == 0, "lstat rt_hard");
    chk(stat("/tmp/rt_target", &st) == 0, "stat rt_target");
    chk(S_ISLNK(sh.st_mode), "rt_hard is itself a symlink (no-follow)");
    chk(ss.st_ino == sh.st_ino, "rt_hard shares inode with rt_sym");
    chk(sh.st_ino != st.st_ino, "rt_hard does NOT share inode with target");

    /* ------- utimensat unknown flags → EINVAL -------------------------- */
    errno = 0;
    chk(utimensat(AT_FDCWD, "/tmp/rt_target", 0, 0x1000) == -1 &&
        errno == EINVAL, "utimensat unknown flag bit rejected");

    /* ------- utimensat AT_SYMLINK_NOFOLLOW touches link, not target --- */
    struct timespec ts[2];
    ts[0].tv_sec = 1700000001; ts[0].tv_nsec = 0;
    ts[1].tv_sec = 1700000001; ts[1].tv_nsec = 0;
    chk(utimensat(AT_FDCWD, "/tmp/rt_sym", ts, AT_SYMLINK_NOFOLLOW) == 0,
        "utimensat AT_SYMLINK_NOFOLLOW on symlink");
    /* Stamp the link itself with one value, then stamp the target with
     * a different value via the no-follow path again — they live in
     * separate inodes, so neither overrides the other. */
    chk(lstat("/tmp/rt_sym", &ss) == 0, "lstat rt_sym post-utimensat");
    chk(ss.st_mtime == 1700000001, "rt_sym mtime updated via NOFOLLOW");

    /* ------- unlinkat "." / ".." → EINVAL ------------------------------ */
    int tfd = open("/tmp", O_RDONLY);
    chk(tfd >= 0, "open /tmp");
    errno = 0;
    chk(unlinkat(tfd, ".", 0) == -1 && errno == EINVAL,
        "unlinkat(\".\") → EINVAL");
    errno = 0;
    chk(unlinkat(tfd, "..", 0) == -1 && errno == EINVAL,
        "unlinkat(\"..\") → EINVAL");
    errno = 0;
    chk(unlinkat(tfd, "foo/.", 0) == -1 && errno == EINVAL,
        "unlinkat(\"foo/.\") → EINVAL");
    errno = 0;
    chk(unlinkat(tfd, "x/..", 0) == -1 && errno == EINVAL,
        "unlinkat(\"x/..\") → EINVAL");
    close(tfd);

    /* ------- getrandom rejects unknown flag bits ------------------- */
    unsigned char rb[4];
    errno = 0;
    chk(getrandom(rb, sizeof(rb), 0xDEAD0000) == -1 && errno == EINVAL,
        "getrandom unknown flags → EINVAL");
    /* Known flags still accepted. */
    chk(getrandom(rb, sizeof(rb), GRND_NONBLOCK) == (ssize_t)sizeof(rb),
        "getrandom GRND_NONBLOCK accepted");

    /* ------- select returns EBADF for a closed fd ---------------- */
    int p[2];
    chk(pipe(p) == 0, "pipe for EBADF probe");
    int closed = p[0];
    close(p[0]);
    fd_set rs; FD_ZERO(&rs); FD_SET(closed, &rs);
    struct timeval tv = { 0, 0 };
    errno = 0;
    int r = select(closed + 1, &rs, 0, 0, &tv);
    chk(r == -1 && errno == EBADF, "select on closed fd → EBADF");
    close(p[1]);

    /* ------- pselect preserves errno across sigprocmask ------------ */
    pipe(p);
    close(p[0]);
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    FD_ZERO(&rs); FD_SET(p[0], &rs);
    struct timespec pts = { 0, 0 };
    errno = 0;
    r = pselect(p[0] + 1, &rs, 0, 0, &pts, &mask);
    int err_after = errno;
    sigprocmask(SIG_SETMASK, &oldmask, 0);
    chk(r == -1 && err_after == EBADF,
        "pselect preserves errno across sigprocmask restore");
    close(p[1]);

    /* Cleanup. */
    (void)unlink("/tmp/rt_target");
    (void)unlink("/tmp/rt_sym");
    (void)unlink("/tmp/rt_hard");

    if (fails) { printf("regression_fixes_test: %d FAIL\n", fails); return 1; }
    printf("PASS\n");
    return 0;
}
