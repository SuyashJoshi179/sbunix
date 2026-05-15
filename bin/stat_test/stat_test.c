#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

int main(void) {
    int fd = open("/bin/init", O_RDONLY);
    if (fd < 0) {
        printf("stat_test: FAIL open /bin/init: %d\n", fd);
        return 1;
    }

    struct stat st;
    int rc = fstat(fd, &st);
    close(fd);

    if (rc < 0) {
        printf("stat_test: FAIL fstat returned %d\n", rc);
        return 1;
    }

    if (!S_ISREG(st.st_mode)) {
        printf("stat_test: FAIL /bin/init not S_IFREG, mode=0x%x\n", st.st_mode);
        return 1;
    }
    if (st.st_size == 0) {
        printf("stat_test: FAIL /bin/init size is 0\n");
        return 1;
    }

    /* Read-only-fs mtime must be non-zero so POSIX make and friends
     * don't treat archived files as "never built". tarfs sources it
     * from the tar header (or, failing that, boot wall-clock). */
    uint64_t tarfs_mtime = st.st_mtime;
    if (tarfs_mtime == 0) {
        printf("stat_test: FAIL /bin/init st_mtime is 0 (read-only fs lost timestamp)\n");
        return 1;
    }
    printf("stat_test: /bin/init (tarfs) st_mtime=%lu\n", (unsigned long)tarfs_mtime);

    /* Writable-storage mtime must be a plausible current wall-clock
     * value — sbfs sources it from the Goldfish RTC at create time. */
    (void)unlink("/mnt/stprobe");
    fd = open("/mnt/stprobe", O_WRONLY | O_CREAT);
    if (fd < 0) {
        printf("stat_test: FAIL open /mnt/stprobe: %d\n", fd);
        return 1;
    }
    if (write(fd, "x", 1) != 1) {
        printf("stat_test: FAIL write probe\n");
        close(fd);
        return 1;
    }
    rc = fstat(fd, &st);
    close(fd);
    if (rc < 0) {
        printf("stat_test: FAIL fstat probe: %d\n", rc);
        return 1;
    }
    if (st.st_mtime == 0) {
        printf("stat_test: FAIL /mnt/stprobe st_mtime is 0 (writable fs lost timestamp)\n");
        (void)unlink("/mnt/stprobe");
        return 1;
    }

    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) == 0) {
        /* Probe was just created — its mtime should be within a few
         * seconds of the real clock. Skews larger than that mean the
         * fs is hard-coding a fake timestamp instead of reading the
         * RTC. Wide window because tests run on a slow QEMU. */
        long delta = (long)now.tv_sec - (long)st.st_mtime;
        if (delta < 0) delta = -delta;
        if (delta > 60) {
            printf("stat_test: FAIL probe mtime=%lu but clock=%lu (delta=%ld)\n",
                   (unsigned long)st.st_mtime, (unsigned long)now.tv_sec, delta);
            (void)unlink("/mnt/stprobe");
            return 1;
        }
    }
    printf("stat_test: /mnt/stprobe (sbfs) st_mtime=%lu\n",
           (unsigned long)st.st_mtime);

    (void)unlink("/mnt/stprobe");

    printf("stat_test: PASS\n");
    return 0;
}
