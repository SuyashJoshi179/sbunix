// fd_limits_test: verify per-process fd table limits and error conditions.
//
// Tests:
//   1. Can open up to NOFILE-3 = 13 extra files (fds 3..15 after stdio).
//   2. The (NOFILE-3+1) = 14th extra open returns a negative error (EMFILE).
//   3. After closing all extra fds, new opens succeed again.
//   4. Opening a non-existent path returns a negative error (ENOENT).
//   5. Opening with a file-as-directory component returns negative (ENOTDIR).
//   6. close() on an invalid fd returns negative (EBADF).
//   7. write() to an invalid fd (99) returns negative (EBADF).
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

static int pass_cnt = 0, fail_cnt = 0;

static void chk(int cond, const char *msg) {
    if (cond) { printf("[fd_limits_test] PASS %s\n", msg); pass_cnt++; }
    else       { printf("[fd_limits_test] FAIL %s\n", msg); fail_cnt++; }
}

// Per-process fd table is NOFILE=16; fds 0/1/2 are stdin/stdout/stderr.
// So we can open 13 more files (fds 3–15) before hitting EMFILE.
#define EXTRA_FDS  13

int main(void) {
    int fds[EXTRA_FDS];
    int i;

    // 1. Open 13 files — all should succeed.
    for (i = 0; i < EXTRA_FDS; i++) {
        fds[i] = open("/etc/rc", O_RDONLY);
        if (fds[i] < 0) {
            printf("[fd_limits_test] FAIL open #%d returned %d (expected >= 0)\n",
                   i, fds[i]);
            fail_cnt++;
            // Close what we opened so far and bail out.
            for (int j = 0; j < i; j++) close(fds[j]);
            goto done;
        }
    }
    pass_cnt++;
    printf("[fd_limits_test] PASS opened %d extra fds\n", EXTRA_FDS);

    // 2. One more open should fail with EMFILE (all 16 slots taken).
    int extra = open("/etc/rc", O_RDONLY);
    chk(extra < 0, "EMFILE: 14th extra open returns negative");
    if (extra >= 0) close(extra);   // clean up if unexpectedly succeeded

    // 3. Close all extra fds; a subsequent open must succeed.
    for (i = 0; i < EXTRA_FDS; i++) close(fds[i]);

    int reopen = open("/etc/rc", O_RDONLY);
    chk(reopen >= 0, "after closing extras, open succeeds again");
    if (reopen >= 0) close(reopen);

    // 4. ENOENT: non-existent path.
    int bad = open("/no/such/file", O_RDONLY);
    chk(bad < 0, "ENOENT: open missing file returns negative");
    if (bad >= 0) close(bad);

    // 5. ENOTDIR: file used as a directory component.
    bad = open("/etc/rc/impossible", O_RDONLY);
    chk(bad < 0, "ENOTDIR: file as dir component returns negative");
    if (bad >= 0) close(bad);

    // 6. EBADF: close() on an invalid fd.
    int rc = close(99);
    chk(rc < 0, "EBADF: close(99) returns negative");

    // 7. EBADF: write() to an invalid fd.
    long n = write(99, "x", 1);
    chk(n < 0, "EBADF: write(99,...) returns negative");

done:
    if (fail_cnt == 0)
        printf("fd_limits_test: PASS (%d tests)\n", pass_cnt);
    else
        printf("fd_limits_test: FAIL (%d/%d failed)\n", fail_cnt, pass_cnt + fail_cnt);
    return fail_cnt ? 1 : 0;
}
