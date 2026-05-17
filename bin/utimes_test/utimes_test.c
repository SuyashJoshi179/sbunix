#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

/* utimensat: writable fs updates mtime; read-only fs returns EROFS
 * (design spec §10 Q1). Probe tmpfs (/tmp), sbfs (/mnt), and tarfs
 * (/bin/sh). The sbfs path is the regression: it reads its mtime from
 * the on-disk dinode (not vnode.mtime), so the kernel ->setmtime hook
 * must mirror vnode.mtime into si->d.mtime AND iupdate, otherwise a
 * subsequent stat returns the stale value. */
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

    /* sbfs regression — same flow as tmpfs above but on /mnt. Pre-fix,
     * stat would return the original create-time mtime because
     * sys_utimensat only updated vnode.mtime and sbfs_op_stat reads
     * si->d.mtime. */
    (void)unlink("/mnt/utimes.tmp");
    fd = open("/mnt/utimes.tmp", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("FAIL: open sbfs errno=%d\n", errno); return 1; }
    close(fd);

    ts[1].tv_sec = 1700000123;
    if (utimensat(AT_FDCWD, "/mnt/utimes.tmp", ts, 0) != 0) {
        printf("FAIL: utimensat sbfs errno=%d\n", errno); return 1;
    }
    if (stat("/mnt/utimes.tmp", &st) < 0) {
        printf("FAIL: stat sbfs errno=%d\n", errno); return 1;
    }
    if (st.st_mtime != 1700000123) {
        printf("FAIL: sbfs mtime not persisted, got %ld want 1700000123\n",
               (long)st.st_mtime);
        return 1;
    }
    (void)unlink("/mnt/utimes.tmp");

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
