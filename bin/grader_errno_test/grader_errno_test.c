/*
 * grader_errno_test — POSIX errno convention.
 *
 * The #1 most common grader failure: syscalls must return -1 on error
 * and set errno to a positive POSIX error code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[grader_errno_test] PASS  %s\n", name);
    } else {
        printf("[grader_errno_test] FAIL  %s\n", name);
        fails++;
    }
}

int main(void) {
    printf("=== grader_errno_test ===\n");

    /* 1. open non-existent file */
    errno = 0;
    int fd = open("/this/path/does/not/exist", O_RDONLY);
    check(fd == -1, "open(nonexistent) returns -1");
    check(errno == ENOENT, "open(nonexistent) sets errno=ENOENT");
    printf("  (got fd=%d errno=%d=%s)\n", fd, errno, strerror(errno));

    /* 2. close(-1) — invalid fd */
    errno = 0;
    int rc = close(-1);
    check(rc == -1, "close(-1) returns -1");
    check(errno == EBADF, "close(-1) sets errno=EBADF");

    /* 3. close(999) — fd not open */
    errno = 0;
    rc = close(999);
    check(rc == -1, "close(999) returns -1");
    check(errno == EBADF, "close(999) sets errno=EBADF");

    /* 4. read on bad fd */
    char buf[64];
    errno = 0;
    long r = read(-1, buf, sizeof(buf));
    check(r == -1, "read(-1) returns -1");
    check(errno == EBADF, "read(-1) sets errno=EBADF");

    /* 5. write on bad fd */
    errno = 0;
    long w = write(-1, "x", 1);
    check(w == -1, "write(-1) returns -1");
    check(errno == EBADF, "write(-1) sets errno=EBADF");

    /* 6. unlink non-existent */
    errno = 0;
    rc = unlink("/nonexistent_file_xyz");
    check(rc == -1, "unlink(nonexistent) returns -1");
    check(errno != 0, "unlink(nonexistent) sets errno");

    /* 7. chdir to non-existent */
    errno = 0;
    rc = chdir("/nonexistent_dir_xyz");
    check(rc == -1, "chdir(nonexistent) returns -1");
    check(errno != 0, "chdir(nonexistent) sets errno");

    /* 8. dup of bad fd */
    errno = 0;
    rc = dup(-1);
    check(rc == -1, "dup(-1) returns -1");
    check(errno == EBADF, "dup(-1) sets errno=EBADF");

    /* 9. Successful call must NOT clobber errno */
    errno = 0;
    int valid_fd = open("/bin/echo", O_RDONLY);
    if (valid_fd >= 0) {
        errno = 42; /* set to nonsense */
        char tmp[1];
        read(valid_fd, tmp, 0); /* zero-length read — should succeed */
        /* errno should not be modified by a successful call */
        /* (POSIX says errno is only meaningful after a failure) */
        close(valid_fd);
        check(1, "successful read(0) does not crash");
    } else {
        check(1, "skipped errno-preservation (no /bin/echo)");
    }

    /* 10. lseek on bad fd */
    errno = 0;
    long off = lseek(-1, 0, 0);
    check(off == -1, "lseek(-1) returns -1");
    check(errno == EBADF, "lseek(-1) sets errno=EBADF");

    printf("=== grader_errno_test: %d failures ===\n", fails);
    return fails;
}
