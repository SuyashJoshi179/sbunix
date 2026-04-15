// path_test: verify VFS path resolution edge cases and inode-type fstat.
//
// Tests:
//   1. Double leading slash: "//etc/rc" resolves like "/etc/rc".
//   2. Dotdot in path: "/bin/../etc/rc" resolves to the rc file.
//   3. fstat on a directory fd returns S_ISDIR.
//   4. fstat on /dev/console returns S_ISCHR.
//   5. Trailing slash on directory: "/bin/" opens successfully.
//   6. ENOTDIR: a file component used as directory is rejected.
//   7. EROFS: writing to a read-only tarfs file returns negative.
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static int pass_cnt = 0, fail_cnt = 0;

static void chk(int cond, const char *msg) {
    if (cond) { printf("[path_test] PASS %s\n", msg); pass_cnt++; }
    else       { printf("[path_test] FAIL %s\n", msg); fail_cnt++; }
}

int main(void) {
    char buf[16];
    int fd;
    long n;

    // 1. Double slash: "//etc/rc" must open just like "/etc/rc".
    fd = open("//etc/rc", O_RDONLY);
    chk(fd >= 0, "double-slash path opens successfully");
    if (fd >= 0) close(fd);

    // 2. Dotdot traversal: "/bin/../etc/rc" must reach /etc/rc.
    fd = open("/bin/../etc/rc", O_RDONLY);
    chk(fd >= 0, "dotdot path /bin/../etc/rc opens successfully");
    if (fd >= 0) {
        n = read(fd, buf, 2);
        // /etc/rc starts with "# " (hash-space).
        chk(n == 2 && buf[0] == '#', "dotdot path reads correct first byte");
        close(fd);
    }

    // 3. fstat on a directory fd must report S_ISDIR.
    fd = open("/bin", O_RDONLY);
    chk(fd >= 0, "open /bin directory succeeds");
    if (fd >= 0) {
        struct stat st;
        int rc = fstat(fd, &st);
        chk(rc == 0,             "fstat /bin returns 0");
        chk(S_ISDIR(st.st_mode), "fstat /bin mode is S_ISDIR");
        close(fd);
    }

    // 4. fstat on /dev/console must report S_ISCHR.
    fd = open("/dev/console", O_RDONLY);
    chk(fd >= 0, "open /dev/console succeeds");
    if (fd >= 0) {
        struct stat st;
        int rc = fstat(fd, &st);
        chk(rc == 0,             "fstat /dev/console returns 0");
        chk(S_ISCHR(st.st_mode), "fstat /dev/console mode is S_ISCHR");
        close(fd);
    }

    // 5. Trailing slash on a directory path must succeed.
    fd = open("/bin/", O_RDONLY);
    chk(fd >= 0, "trailing slash on directory path opens successfully");
    if (fd >= 0) close(fd);

    // 6. ENOTDIR: using a regular file as a directory component.
    fd = open("/etc/rc/nope", O_RDONLY);
    chk(fd < 0, "ENOTDIR: file-as-dir-component returns negative");
    if (fd >= 0) close(fd);

    // 7. EROFS: sys_open permits opening tarfs for write, but write() must fail.
    fd = open("/etc/rc", O_WRONLY);
    if (fd >= 0) {
        n = write(fd, "x", 1);
        chk(n < 0, "EROFS: write to read-only tarfs file returns negative");
        close(fd);
    } else {
        // If the kernel rejected the O_WRONLY open itself, that is also acceptable.
        chk(1, "EROFS: open O_WRONLY on tarfs rejected at open (also acceptable)");
    }

    if (fail_cnt == 0)
        printf("path_test: PASS (%d tests)\n", pass_cnt);
    else
        printf("path_test: FAIL (%d/%d failed)\n", fail_cnt, pass_cnt + fail_cnt);
    return fail_cnt ? 1 : 0;
}
