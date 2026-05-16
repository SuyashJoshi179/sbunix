#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

/* Linux defines these in fcntl.h; SBUnix may not, depending on header
 * version. Provide local fallbacks so the test stays self-contained. */
#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif

/* linkat / renameat / symlinkat / readlinkat: dirfd-relative variants
 * of the corresponding non-`at` syscalls. Verify each against tmpfs
 * (/tmp), which is the writable scratch fs registered at boot. */
int main(void) {
    /* Open /tmp as the dirfd we'll use for all relative operations. */
    int tfd = open("/tmp", O_RDONLY);
    if (tfd < 0) { printf("FAIL: open /tmp errno=%d\n", errno); return 1; }

    /* Clean any residue from prior runs. */
    unlinkat(tfd, "atr_a", 0);
    unlinkat(tfd, "atr_b", 0);
    unlinkat(tfd, "atr_sym", 0);
    unlinkat(tfd, "atr_c", 0);

    /* Create atr_a under /tmp via openat. */
    int afd = openat(tfd, "atr_a", O_CREAT | O_WRONLY);
    if (afd < 0) { printf("FAIL: openat atr_a errno=%d\n", errno); return 1; }
    if (write(afd, "hi", 2) != 2) {
        printf("FAIL: write atr_a errno=%d\n", errno);
        close(afd);
        return 1;
    }
    close(afd);

    /* linkat: create /tmp/atr_b as hard link to /tmp/atr_a. */
    if (linkat(tfd, "atr_a", tfd, "atr_b", 0) < 0) {
        printf("FAIL: linkat errno=%d\n", errno);
        return 1;
    }
    struct stat sa, sb;
    if (fstatat(tfd, "atr_a", &sa, 0) < 0 ||
        fstatat(tfd, "atr_b", &sb, 0) < 0) {
        printf("FAIL: fstatat after linkat errno=%d\n", errno);
        return 1;
    }
    if (sa.st_ino != sb.st_ino) {
        printf("FAIL: linkat ino mismatch a=%ld b=%ld\n",
               (long)sa.st_ino, (long)sb.st_ino);
        return 1;
    }

    /* renameat: atr_b → atr_c. */
    if (renameat(tfd, "atr_b", tfd, "atr_c") < 0) {
        printf("FAIL: renameat errno=%d\n", errno);
        return 1;
    }
    if (fstatat(tfd, "atr_b", &sb, 0) == 0) {
        printf("FAIL: atr_b still present after renameat\n");
        return 1;
    }
    if (fstatat(tfd, "atr_c", &sb, 0) < 0) {
        printf("FAIL: atr_c missing after renameat errno=%d\n", errno);
        return 1;
    }

    /* symlinkat: create atr_sym pointing at "atr_a". */
    if (symlinkat("atr_a", tfd, "atr_sym") < 0) {
        printf("FAIL: symlinkat errno=%d\n", errno);
        return 1;
    }

    /* readlinkat: read it back. */
    char buf[64] = {0};
    ssize_t n = readlinkat(tfd, "atr_sym", buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("FAIL: readlinkat errno=%d\n", errno);
        return 1;
    }
    buf[n] = 0;
    if (strcmp(buf, "atr_a") != 0) {
        printf("FAIL: readlinkat got '%s' want 'atr_a'\n", buf);
        return 1;
    }

    /* AT_FDCWD path: chdir into /tmp then exercise the same operations
     * with AT_FDCWD to verify the dirfd_to_inode(AT_FDCWD) branch. */
    char cwd_save[256];
    if (!getcwd(cwd_save, sizeof(cwd_save))) {
        printf("FAIL: getcwd errno=%d\n", errno);
        return 1;
    }
    if (chdir("/tmp") < 0) {
        printf("FAIL: chdir /tmp errno=%d\n", errno);
        return 1;
    }
    n = readlinkat(AT_FDCWD, "atr_sym", buf, sizeof(buf) - 1);
    if (n < 0 || strncmp(buf, "atr_a", 5) != 0) {
        printf("FAIL: readlinkat AT_FDCWD n=%ld errno=%d\n",
               (long)n, errno);
        return 1;
    }
    chdir(cwd_save);

    /* Non-directory dirfd → ENOTDIR. */
    int rfd = open("/tmp/atr_a", O_RDONLY);
    if (rfd < 0) { printf("FAIL: open atr_a for ENOTDIR\n"); return 1; }
    errno = 0;
    if (readlinkat(rfd, "x", buf, sizeof(buf)) >= 0 || errno != ENOTDIR) {
        printf("FAIL: readlinkat non-dir dirfd errno=%d want ENOTDIR\n",
               errno);
        return 1;
    }
    close(rfd);

    /* Cleanup. */
    unlinkat(tfd, "atr_a", 0);
    unlinkat(tfd, "atr_c", 0);
    unlinkat(tfd, "atr_sym", 0);
    close(tfd);

    printf("PASS\n");
    return 0;
}
