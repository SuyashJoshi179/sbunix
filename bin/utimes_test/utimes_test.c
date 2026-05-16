#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

/* utimensat: writable fs updates mtime; read-only fs returns EROFS
 * (design spec §10 Q1). Probe both via tmpfs (/tmp) and tarfs (/bin/sh). */
int main(void) {
    (void)unlink("/tmp/utimes_test.tmp");
    int fd = open("/tmp/utimes_test.tmp", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("FAIL: open tmpfs errno=%d\n", errno); return 1; }
    close(fd);

    struct timespec ts[2];
    ts[0].tv_sec = 1700000000; ts[0].tv_nsec = 0;
    ts[1].tv_sec = 1700000000; ts[1].tv_nsec = 0;
    if (utimensat(AT_FDCWD, "/tmp/utimes_test.tmp", ts, 0) != 0) {
        printf("FAIL: utimensat tmpfs errno=%d\n", errno); return 1;
    }

    struct stat st;
    if (stat("/tmp/utimes_test.tmp", &st) < 0) {
        printf("FAIL: stat errno=%d\n", errno); return 1;
    }
    if (st.st_mtime != 1700000000) {
        printf("FAIL: mtime not persisted, got %ld\n", (long)st.st_mtime);
        return 1;
    }
    (void)unlink("/tmp/utimes_test.tmp");

    /* tarfs is read-only — utimensat must reject with EROFS. */
    errno = 0;
    int r = utimensat(AT_FDCWD, "/bin/sh", ts, 0);
    if (r == 0) {
        printf("FAIL: utimensat on tarfs unexpectedly succeeded\n");
        return 1;
    }
    if (errno != EROFS) {
        printf("FAIL: utimensat tarfs errno=%d (want EROFS=%d)\n",
               errno, EROFS);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
