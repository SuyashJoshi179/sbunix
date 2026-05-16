#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

/* openat(2): a real dirfd must resolve the relative path against that
 * directory, not against cwd. AT_FDCWD path falls back to plain open. */
int main(void) {
    int dfd = open("/bin", O_RDONLY);
    if (dfd < 0) {
        printf("FAIL: open /bin errno=%d\n", errno);
        return 1;
    }

    int fd = openat(dfd, "sh", O_RDONLY);
    if (fd < 0) {
        printf("FAIL: openat(dirfd=/bin, \"sh\") errno=%d\n", errno);
        close(dfd);
        return 1;
    }
    char buf[4];
    int n = read(fd, buf, 4);
    if (n != 4 || buf[0] != 0x7f || buf[1] != 'E' ||
        buf[2] != 'L' || buf[3] != 'F') {
        printf("FAIL: /bin/sh via openat lacks ELF magic (n=%d)\n", n);
        return 1;
    }
    close(fd);
    close(dfd);

    /* AT_FDCWD still works */
    fd = openat(AT_FDCWD, "/bin/sh", O_RDONLY);
    if (fd < 0) {
        printf("FAIL: openat(AT_FDCWD, /bin/sh) errno=%d\n", errno);
        return 1;
    }
    close(fd);

    /* dirfd on a non-directory must report ENOTDIR */
    int rfd = open("/bin/sh", O_RDONLY);
    if (rfd < 0) {
        printf("FAIL: open /bin/sh errno=%d\n", errno);
        return 1;
    }
    errno = 0;
    int bad = openat(rfd, "anything", O_RDONLY);
    if (bad >= 0 || errno != ENOTDIR) {
        printf("FAIL: openat on non-dir dirfd: rc=%d errno=%d (want ENOTDIR=%d)\n",
               bad, errno, ENOTDIR);
        return 1;
    }
    close(rfd);

    printf("PASS\n");
    return 0;
}
