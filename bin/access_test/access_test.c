#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/* Probes access() against a known-good path (/bin/sh, mode 0755 in tarfs)
 * and a known-bad path. Returns 0 on PASS, 1 on FAIL. With the libc stub
 * this whole file fails on the very first check; once SYS_access is wired
 * to namei it should pass cleanly. */

static int expect_ok(const char *path, int mode, const char *label) {
    errno = 0;
    int r = access(path, mode);
    if (r != 0) {
        printf("access_test: %s: access(\"%s\", %d) = %d errno=%d (want 0)\n",
               label, path, mode, r, errno);
        return 1;
    }
    return 0;
}

static int expect_err(const char *path, int mode, int want_errno, const char *label) {
    errno = 0;
    int r = access(path, mode);
    if (r != -1 || errno != want_errno) {
        printf("access_test: %s: access(\"%s\", %d) = %d errno=%d (want -1/%d)\n",
               label, path, mode, r, errno, want_errno);
        return 1;
    }
    return 0;
}

int main(void) {
    int fails = 0;

    fails += expect_ok("/bin/sh", F_OK,                 "exists");
    fails += expect_ok("/bin/sh", R_OK,                 "readable");
    fails += expect_ok("/bin/sh", X_OK,                 "executable");
    fails += expect_ok("/bin/sh", F_OK | R_OK | X_OK,   "exists+R+X");

    fails += expect_err("/no/such/path",    F_OK, ENOENT, "missing-FOK");
    fails += expect_err("/no/such/path",    R_OK, ENOENT, "missing-ROK");
    fails += expect_err("/bin/nosuchthing", X_OK, ENOENT, "bin-missing");

    /* Unknown mode bits → EINVAL. */
    fails += expect_err("/bin/sh", 0x40, EINVAL, "bogus-mode");

    if (fails) {
        printf("access_test: FAIL (%d checks)\n", fails);
        return 1;
    }
    printf("access_test: PASS\n");
    return 0;
}
