#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;
static void check(int cond, const char *name) {
    if (cond) printf("[symlink_test] PASS  %s\n", name);
    else    { printf("[symlink_test] FAIL  %s\n", name); fails++; }
}

int main(void) {
    printf("=== symlink_test ===\n");

    /* readlink on /dev/loop returns the literal target. */
    char buf[64] = {0};
    long n = readlink("/dev/loop", buf, sizeof(buf) - 1);
    check(n == 9, "readlink length == 9");
    if (n > 0) buf[n] = 0;
    check(strcmp(buf, "/dev/loop") == 0, "readlink target string");

    /* readlink on a non-symlink returns -1, errno=EINVAL. */
    long r = readlink("/dev/console", buf, sizeof(buf));
    check(r == -1 && errno == EINVAL, "readlink on non-symlink → EINVAL");

    /* readlink on a missing path returns -1, errno=ENOENT. */
    r = readlink("/dev/no_such_thing", buf, sizeof(buf));
    check(r == -1 && errno == ENOENT, "readlink on missing path → ENOENT");

    /* Buffer truncation: target is 9 bytes, ask for 4 → 4 bytes, no NUL. */
    char tbuf[16];
    for (int i = 0; i < (int)sizeof(tbuf); i++) tbuf[i] = (char)0xAA;
    r = readlink("/dev/loop", tbuf, 4);
    check(r == 4, "truncated readlink returns 4");
    check(tbuf[4] == (char)0xAA, "truncated readlink writes no NUL");

    /* lstat reports S_IFLNK on the link itself. */
    struct stat st;
    int rc = lstat("/dev/loop", &st);
    check(rc == 0, "lstat /dev/loop succeeds");
    check(S_ISLNK(st.st_mode), "lstat /dev/loop is S_ISLNK");

    /* open follows the symlink → ELOOP because target is itself. */
    int fd = open("/dev/loop", 0);
    check(fd == -1 && errno == ELOOP, "open(/dev/loop) → ELOOP");

    /* lstat on a non-link still works (regression check). */
    rc = lstat("/dev/console", &st);
    check(rc == 0, "lstat /dev/console succeeds");
    check(S_ISCHR(st.st_mode), "lstat /dev/console is S_ISCHR");

    /* ---- symlink(2) creation flows ---- */

    /* Pre-clean any prior run's leftovers. */
    unlink("/tmp/lnk");
    unlink("/tmp/realfile");
    unlink("/mnt/lnk");
    unlink("/mnt/realfile");

    /* Create a target file in tmpfs and a symlink pointing at it. */
    int wfd = open("/tmp/realfile", 0x42 /* O_WRONLY|O_CREAT */);
    check(wfd >= 0, "create /tmp/realfile");
    if (wfd >= 0) { write(wfd, "hello", 5); close(wfd); }

    rc = symlink("/tmp/realfile", "/tmp/lnk");
    check(rc == 0, "symlink /tmp/realfile -> /tmp/lnk (tmpfs)");

    /* readlink returns the original target. */
    char buf2[64] = {0};
    long n2 = readlink("/tmp/lnk", buf2, sizeof(buf2) - 1);
    if (n2 > 0 && n2 < (long)sizeof(buf2)) buf2[n2] = 0;
    check(n2 == 13 && strcmp(buf2, "/tmp/realfile") == 0,
          "readlink returns /tmp/realfile");

    /* lstat reports S_IFLNK on the new link. */
    rc = lstat("/tmp/lnk", &st);
    check(rc == 0 && S_ISLNK(st.st_mode), "lstat /tmp/lnk is S_ISLNK");

    /* open() follows the link and lands on the regular file. */
    int rfd = open("/tmp/lnk", 0);
    check(rfd >= 0, "open(/tmp/lnk) follows the symlink");
    if (rfd >= 0) {
        char rbuf[8] = {0};
        long got = read(rfd, rbuf, 5);
        check(got == 5 && memcmp(rbuf, "hello", 5) == 0,
              "read through symlink returns file contents");
        close(rfd);
    }

    /* EEXIST on duplicate create. */
    rc = symlink("/anything", "/tmp/lnk");
    check(rc == -1 && errno == EEXIST, "symlink onto existing path → EEXIST");

    /* Same flow on sbfs (writable on-disk fs). */
    wfd = open("/mnt/realfile", 0x42);
    check(wfd >= 0, "create /mnt/realfile");
    if (wfd >= 0) { write(wfd, "world", 5); close(wfd); }

    rc = symlink("/mnt/realfile", "/mnt/lnk");
    check(rc == 0, "symlink /mnt/realfile -> /mnt/lnk (sbfs)");

    char buf3[64] = {0};
    long n3 = readlink("/mnt/lnk", buf3, sizeof(buf3) - 1);
    if (n3 > 0 && n3 < (long)sizeof(buf3)) buf3[n3] = 0;
    check(n3 == 13 && strcmp(buf3, "/mnt/realfile") == 0,
          "sbfs readlink returns /mnt/realfile");

    /* Read via the link. */
    rfd = open("/mnt/lnk", 0);
    check(rfd >= 0, "open(/mnt/lnk) follows the symlink");
    if (rfd >= 0) {
        char rbuf[8] = {0};
        long got = read(rfd, rbuf, 5);
        check(got == 5 && memcmp(rbuf, "world", 5) == 0,
              "read through sbfs symlink returns file contents");
        close(rfd);
    }

    /* EROFS on tarfs (read-only). */
    rc = symlink("/wherever", "/bin/lnk");
    check(rc == -1 && errno == EROFS, "symlink on tarfs → EROFS");

    /* Cleanup */
    unlink("/tmp/lnk");
    unlink("/tmp/realfile");
    unlink("/mnt/lnk");
    unlink("/mnt/realfile");

    printf("=== symlink_test: %d failures ===\n", fails);
    return fails;
}
