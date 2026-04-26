/* Verify that opening a read-only filesystem (tarfs) for write fails at
 * open() time, not silently at write() time. Reproduces the
 * `echo hi > /bin/echo` bug where the shell saw no error because the
 * open succeeded against a tarfs file. */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static int fail_count = 0;

#define CHECK(cond) do {                                                  \
    if (!(cond)) { fail_count++;                                          \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

int main(void) {
    /* Read-only sanity: must succeed. */
    int fd = open("/bin/echo", O_RDONLY);
    CHECK(fd >= 0);
    if (fd >= 0) close(fd);

    /* Write opens against tarfs files must be rejected at open() time. */
    fd = open("/bin/echo", O_WRONLY);
    CHECK(fd < 0);
    if (fd >= 0) close(fd);

    fd = open("/bin/echo", O_WRONLY | O_TRUNC);
    CHECK(fd < 0);
    if (fd >= 0) close(fd);

    fd = open("/bin/echo", O_WRONLY | O_CREAT | O_TRUNC);
    CHECK(fd < 0);
    if (fd >= 0) close(fd);

    fd = open("/bin/echo", O_RDWR);
    CHECK(fd < 0);
    if (fd >= 0) close(fd);

    /* Sbfs file: write opens must succeed. */
    fd = open("/data/rofs_probe.txt", O_WRONLY | O_CREAT | O_TRUNC);
    CHECK(fd >= 0);
    if (fd >= 0) {
        write(fd, "ok", 2);
        close(fd);
    }

    if (fail_count == 0) {
        puts("rofs_test: PASS");
        return 0;
    }
    fprintf(stderr, "rofs_test: FAIL (%d errors)\n", fail_count);
    return 1;
}
