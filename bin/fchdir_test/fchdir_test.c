#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

/* fchdir(2): change cwd to the directory referenced by an open fd.
 * After fchdir, getcwd should return the path under which the fd was
 * opened. Non-directory fds must report ENOTDIR; bad fds EBADF. */
int main(void) {
    char cwd0[256];
    if (!getcwd(cwd0, sizeof(cwd0))) {
        printf("FAIL: getcwd initial errno=%d\n", errno);
        return 1;
    }

    /* Open /bin and fchdir into it. */
    int bfd = open("/bin", O_RDONLY);
    if (bfd < 0) { printf("FAIL: open /bin errno=%d\n", errno); return 1; }
    if (fchdir(bfd) < 0) {
        printf("FAIL: fchdir(/bin) errno=%d\n", errno);
        return 1;
    }
    char cwd[256];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("FAIL: getcwd post-fchdir errno=%d\n", errno);
        return 1;
    }
    if (strcmp(cwd, "/bin") != 0) {
        printf("FAIL: getcwd='%s' want='/bin'\n", cwd);
        return 1;
    }

    /* Relative open in new cwd: 'sh' should resolve to /bin/sh. */
    int shfd = open("sh", O_RDONLY);
    if (shfd < 0) {
        printf("FAIL: open 'sh' after fchdir(/bin) errno=%d\n", errno);
        return 1;
    }
    char magic[4];
    if (read(shfd, magic, 4) != 4 || magic[0] != 0x7f || magic[1] != 'E') {
        printf("FAIL: 'sh' is not ELF after fchdir\n");
        return 1;
    }
    close(shfd);
    close(bfd);

    /* fchdir on a non-directory fd → ENOTDIR. */
    int rfd = open("/bin/sh", O_RDONLY);
    if (rfd < 0) { printf("FAIL: open /bin/sh\n"); return 1; }
    errno = 0;
    if (fchdir(rfd) >= 0 || errno != ENOTDIR) {
        printf("FAIL: fchdir non-dir errno=%d (want ENOTDIR=%d)\n",
               errno, ENOTDIR);
        return 1;
    }
    close(rfd);

    /* fchdir on a closed/bad fd → EBADF. */
    errno = 0;
    if (fchdir(999) >= 0 || errno != EBADF) {
        printf("FAIL: fchdir bad fd errno=%d\n", errno);
        return 1;
    }

    /* Return to original cwd so subsequent tests aren't disturbed. */
    if (chdir(cwd0) < 0) {
        printf("FAIL: restore cwd errno=%d\n", errno);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
