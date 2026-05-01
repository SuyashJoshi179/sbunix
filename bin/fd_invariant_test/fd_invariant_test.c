#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static int pass;
static int fail;

static void check(int cond, const char *name) {
    if (cond) {
        printf("PASS  %s\n", name);
        pass++;
    } else {
        printf("FAIL  %s\n", name);
        fail++;
    }
}

int main(void) {
    int fd = open("/dev/console", O_RDONLY);
    check(fd >= 0, "open console");
    if (fd < 0) {
        printf("fd_invariant_test: %d passed, %d failed\n", pass, fail + 1);
        return 1;
    }

    check(dup2(fd, fd) == fd, "dup2(fd,fd) is no-op");

    int d = dup(fd);
    check(d >= 0, "dup(fd) succeeds");
    if (d >= 0) {
        check(close(d) == 0, "close duplicated fd");
        check(close(d) == -1 && errno == EBADF, "double close returns -EBADF");
    }

    check(close(fd) == 0, "close original fd");
    char c = 0;
    check(read(fd, &c, 1) == -1 && errno == EBADF, "read on closed fd returns -EBADF");

    int fds[64];
    int n = 0;
    int rc = 0;
    while (n < (int)(sizeof(fds) / sizeof(fds[0]))) {
        rc = open("/dev/console", O_RDONLY);
        if (rc < 0) break;
        fds[n++] = rc;
    }
    check(rc == -1 && errno == EMFILE, "fd table reaches -EMFILE");

    for (int i = 0; i < n; i++)
        close(fds[i]);

    fd = open("/dev/console", O_RDONLY);
    check(fd >= 0, "open works again after cleanup");
    if (fd >= 0) close(fd);

    printf("fd_invariant_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
