/* o_append_test (T2.2): O_APPEND must reset the write offset to EOF
 * before *every* write, not just at open time. The previous behaviour
 * set f->off once at open and then advanced normally — a subsequent
 * lseek() (or any concurrent extension) made appends land in the
 * wrong place. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int fails = 0;
#define CHECK(cond, label) do { \
    if (!(cond)) { printf("FAIL: %s\n", label); fails++; } \
} while (0)

static const char *path = "/tmp/o_append_test";

static int read_file(const char *p, char *buf, int n) {
    int fd = open(p, O_RDONLY);
    if (fd < 0) return -1;
    int total = 0, r;
    while (total < n - 1 && (r = read(fd, buf + total, n - 1 - total)) > 0)
        total += r;
    buf[total] = '\0';
    close(fd);
    return total;
}

int main(void) {
    unlink(path);

    /* Seed file with 'AAA'. */
    int fd = open(path, O_WRONLY | O_CREAT);
    CHECK(fd >= 0, "create seed file");
    if (fd < 0) return 1;
    CHECK(write(fd, "AAA", 3) == 3, "seed write");
    close(fd);

    /* WITHOUT O_APPEND, seek-to-0 + write must overwrite — sanity check
     * that this fixture distinguishes seek-write from append. */
    fd = open(path, O_RDWR);
    CHECK(fd >= 0, "reopen no-append");
    lseek(fd, 0, SEEK_SET);
    CHECK(write(fd, "X", 1) == 1, "overwrite at 0");
    close(fd);

    char buf[32];
    int n = read_file(path, buf, sizeof(buf));
    CHECK(n == 3 && memcmp(buf, "XAA", 3) == 0, "no-append seek overwrites");

    /* WITH O_APPEND, the seek-to-0 must be ignored — the next write goes
     * to EOF. The previous bug let it write at offset 0. */
    fd = open(path, O_RDWR | O_APPEND);
    CHECK(fd >= 0, "reopen O_APPEND");
    lseek(fd, 0, SEEK_SET);
    CHECK(write(fd, "Y", 1) == 1, "append write");
    close(fd);

    n = read_file(path, buf, sizeof(buf));
    CHECK(n == 4 && memcmp(buf, "XAAY", 4) == 0, "O_APPEND ignores lseek before write");

    /* A second write through the same O_APPEND fd should also append,
     * not retain the offset from the previous write. */
    fd = open(path, O_WRONLY | O_APPEND);
    CHECK(fd >= 0, "reopen O_APPEND again");
    CHECK(write(fd, "ZZ", 2) == 2, "second append write");
    lseek(fd, 0, SEEK_SET);
    CHECK(write(fd, "Q", 1) == 1, "post-seek append");
    close(fd);

    n = read_file(path, buf, sizeof(buf));
    CHECK(n == 7 && memcmp(buf, "XAAYZZQ", 7) == 0, "every O_APPEND write goes to EOF");

    unlink(path);

    if (fails == 0) printf("o_append_test: PASS\n");
    else printf("o_append_test: %d FAIL(s)\n", fails);
    return fails;
}
