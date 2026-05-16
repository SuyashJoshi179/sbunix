#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

/* stat(2) must succeed without consuming an fd slot and must follow
 * symlinks (whereas lstat does not). The libc fallback would silently
 * burn an fd on open() — verify by saturating the fd table first and
 * then calling stat(): the kernel SYS_stat path returns success even
 * with zero free fds. */
int main(void) {
    /* tarfs read */
    struct stat st;
    if (stat("/bin/sh", &st) < 0) {
        printf("FAIL: stat(/bin/sh) errno=%d\n", errno); return 1;
    }
    if (!S_ISREG(st.st_mode)) {
        printf("FAIL: /bin/sh mode=%o (want regular)\n", st.st_mode); return 1;
    }
    if (st.st_size <= 0) {
        printf("FAIL: /bin/sh size=%ld\n", (long)st.st_size); return 1;
    }

    /* Saturate fds: a libc fallback that opens before stat() would EMFILE. */
    int fds[60];
    int n = 0;
    while (n < (int)(sizeof(fds)/sizeof(fds[0]))) {
        int fd = open("/bin/sh", O_RDONLY);
        if (fd < 0) break;
        fds[n++] = fd;
    }
    /* Now stat() must still work — direct syscall, no open() needed. */
    if (stat("/bin/sh", &st) < 0) {
        printf("FAIL: stat with saturated fds errno=%d\n", errno);
        for (int i = 0; i < n; i++) close(fds[i]);
        return 1;
    }
    for (int i = 0; i < n; i++) close(fds[i]);

    /* Symlink follow vs no-follow on tmpfs. */
    (void)unlink("/tmp/stat_link");
    (void)unlink("/tmp/stat_target");
    int fd = open("/tmp/stat_target", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        printf("FAIL: open /tmp/stat_target errno=%d\n", errno); return 1;
    }
    if (write(fd, "x", 1) != 1) {
        printf("FAIL: write target errno=%d\n", errno); close(fd); return 1;
    }
    close(fd);
    if (symlink("/tmp/stat_target", "/tmp/stat_link") < 0) {
        printf("FAIL: symlink errno=%d\n", errno); return 1;
    }
    struct stat lst, fst;
    if (lstat("/tmp/stat_link", &lst) < 0) {
        printf("FAIL: lstat link errno=%d\n", errno); return 1;
    }
    if (stat("/tmp/stat_link", &fst) < 0) {
        printf("FAIL: stat link errno=%d\n", errno); return 1;
    }
    if (!S_ISLNK(lst.st_mode)) {
        printf("FAIL: lstat mode=%o (want symlink)\n", lst.st_mode); return 1;
    }
    if (!S_ISREG(fst.st_mode)) {
        printf("FAIL: stat-through-link mode=%o (want regular)\n", fst.st_mode);
        return 1;
    }
    (void)unlink("/tmp/stat_link");
    (void)unlink("/tmp/stat_target");

    /* ENOENT propagation */
    errno = 0;
    if (stat("/tmp/no_such_path_xyz", &st) == 0 || errno != ENOENT) {
        printf("FAIL: stat missing path errno=%d (want ENOENT=%d)\n",
               errno, ENOENT); return 1;
    }

    printf("PASS\n");
    return 0;
}
