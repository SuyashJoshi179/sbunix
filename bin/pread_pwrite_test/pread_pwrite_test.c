#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* pread/pwrite must read/write at an explicit offset without touching
 * the file cursor. Test on sbfs (writable) so we exercise the real I/O
 * path. */
int main(void) {
    (void)unlink("/tmp/pread_pwrite_test.tmp");

    int fd = open("/tmp/pread_pwrite_test.tmp", O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("FAIL: open errno=%d\n", errno); return 1; }

    /* Move cursor to 100 — pwrite/pread must not perturb it. */
    if (lseek(fd, 100, SEEK_SET) != 100) {
        printf("FAIL: lseek SET 100\n"); return 1;
    }

    if (pwrite(fd, "ABCD", 4, 0) != 4) {
        printf("FAIL: pwrite errno=%d\n", errno); return 1;
    }
    if (lseek(fd, 0, SEEK_CUR) != 100) {
        printf("FAIL: pwrite moved cursor\n"); return 1;
    }

    char buf[4] = {0};
    if (pread(fd, buf, 4, 0) != 4) {
        printf("FAIL: pread errno=%d\n", errno); return 1;
    }
    if (memcmp(buf, "ABCD", 4) != 0) {
        printf("FAIL: pread payload mismatch\n"); return 1;
    }
    if (lseek(fd, 0, SEEK_CUR) != 100) {
        printf("FAIL: pread moved cursor\n"); return 1;
    }

    /* Pipe → ESPIPE. */
    int p[2];
    if (pipe(p) < 0) { printf("FAIL: pipe\n"); return 1; }
    if (pwrite(p[1], "x", 1, 0) != -1 || errno != ESPIPE) {
        printf("FAIL: pwrite on pipe should ESPIPE, got errno=%d\n", errno);
        return 1;
    }
    if (pread(p[0], buf, 1, 0) != -1 || errno != ESPIPE) {
        printf("FAIL: pread on pipe should ESPIPE, got errno=%d\n", errno);
        return 1;
    }
    close(p[0]); close(p[1]);
    close(fd);
    (void)unlink("/tmp/pread_pwrite_test.tmp");

    printf("PASS\n");
    return 0;
}
