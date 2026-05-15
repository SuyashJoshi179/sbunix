/*
 * access_test — verify POSIX access(2) probes the VFS correctly.
 *
 * Coverage:
 *   1. F_OK on existing path returns 0
 *   2. F_OK on missing path returns -1 with errno=ENOENT
 *   3. F_OK on each mounted fs (/bin, /mnt, /tmp, /proc) succeeds
 *   4. R_OK on existing readable file succeeds
 *   5. W_OK on tarfs (read-only) file returns -1 with errno=EACCES
 *   6. W_OK on sbfs / tmpfs file succeeds
 *   7. X_OK on a known executable (/bin/echo) succeeds
 *   8. X_OK on a freshly-created non-exec file returns -1 (EACCES)
 *   9. Combined R_OK|W_OK|X_OK on /bin/echo: writable bit denies
 *  10. Invalid mode bits return -1 with errno=EINVAL
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int pass = 0, fail = 0;

static void chk(int cond, const char *msg) {
    if (cond) { printf("[access_test] PASS  %s\n", msg); pass++; }
    else      { printf("[access_test] FAIL  %s\n", msg); fail++; }
}

int main(void) {
    /* ---- 1. F_OK on existing path ---- */
    chk(access("/bin/echo", F_OK) == 0,
        "access(/bin/echo, F_OK) returns 0");

    /* ---- 2. F_OK on missing path ---- */
    errno = 0;
    int rc = access("/no/such/path/xyz", F_OK);
    chk(rc < 0 && errno == ENOENT,
        "access(missing, F_OK) returns -1 with errno=ENOENT");

    /* ---- 3. F_OK on each mount point ---- */
    chk(access("/bin",  F_OK) == 0, "/bin  exists (tarfs root)");
    chk(access("/mnt",  F_OK) == 0, "/mnt  exists (sbfs root)");
    chk(access("/tmp",  F_OK) == 0, "/tmp  exists (tmpfs root)");
    chk(access("/proc", F_OK) == 0, "/proc exists (procfs root)");

    /* Set up: create a writable file in /tmp for the rest of the tests. */
    (void)unlink("/tmp/probe");
    int fd = open("/tmp/probe", O_WRONLY | O_CREAT);
    if (fd >= 0) { write(fd, "hi", 2); close(fd); }

    /* ---- 4. R_OK on existing readable file ---- */
    chk(access("/tmp/probe", R_OK) == 0,
        "access(/tmp/probe, R_OK) returns 0");
    chk(access("/bin/echo",  R_OK) == 0,
        "access(/bin/echo, R_OK) returns 0");

    /* ---- 5. W_OK on tarfs (read-only) ---- */
    errno = 0;
    rc = access("/bin/echo", W_OK);
    chk(rc < 0 && errno == EACCES,
        "access(/bin/echo, W_OK) on tarfs returns -1 EACCES");

    /* ---- 6. W_OK on sbfs / tmpfs ---- */
    chk(access("/tmp/probe", W_OK) == 0,
        "access(/tmp/probe, W_OK) on tmpfs returns 0");

    /* ---- 7. X_OK on a known executable (/bin/echo) ---- */
    chk(access("/bin/echo", X_OK) == 0,
        "access(/bin/echo, X_OK) returns 0 (tarfs marks bins exec)");

    /* ---- 8. X_OK on a fresh non-exec file ---- */
    /* Open with default sbfs mode (0644) → no exec bit. */
    errno = 0;
    rc = access("/tmp/probe", X_OK);
    chk(rc < 0 && errno == EACCES,
        "access(/tmp/probe, X_OK) on 0644 file returns -1 EACCES");

    /* ---- 9. Combined R|W|X on /bin/echo: W denies overall ---- */
    errno = 0;
    rc = access("/bin/echo", R_OK | W_OK | X_OK);
    chk(rc < 0 && errno == EACCES,
        "access(/bin/echo, R|W|X) denies due to W");

    /* ---- 10. Invalid mode bits ---- */
    errno = 0;
    rc = access("/bin/echo", 0x40);   /* bit outside F_OK|R_OK|W_OK|X_OK */
    chk(rc < 0 && errno == EINVAL,
        "access(path, bogus_mode) returns -1 EINVAL");

    (void)unlink("/tmp/probe");

    if (fail == 0) printf("access_test: PASS (%d tests)\n", pass);
    else           printf("access_test: FAIL (%d/%d failed)\n", fail, pass + fail);
    return fail ? 1 : 0;
}
