// o_excl_test: open(O_CREAT|O_EXCL) on an existing file must fail with EEXIST.
//
// POSIX: if O_CREAT and O_EXCL are set, open() shall fail with [EEXIST] when
// the named file exists. Without O_EXCL, the same call shall reopen it.
//
// /tmp is tmpfs (writable, in-RAM) so the test is self-contained.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static int pass_cnt = 0, fail_cnt = 0;

static void chk(int cond, const char *msg) {
    if (cond) { printf("[o_excl_test] PASS %s\n", msg); pass_cnt++; }
    else      { printf("[o_excl_test] FAIL %s\n", msg); fail_cnt++; }
}

int main(void) {
    const char *path = "/tmp/o_excl_test.tmp";
    (void)unlink(path);  /* best-effort cleanup from a prior run */

    /* 1. Fresh create with O_EXCL: must succeed. */
    errno = 0;
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL);
    chk(fd >= 0, "O_CREAT|O_EXCL on new file succeeds");
    if (fd >= 0) close(fd);

    /* 2. Re-create with O_EXCL on the now-existing file: must fail with EEXIST. */
    errno = 0;
    int rc = open(path, O_RDWR | O_CREAT | O_EXCL);
    chk(rc == -1 && errno == EEXIST,
        "O_CREAT|O_EXCL on existing file -> -1/EEXIST");
    if (rc >= 0) close(rc);

    /* 3. O_CREAT without O_EXCL on existing file: must reopen. */
    errno = 0;
    fd = open(path, O_RDWR | O_CREAT);
    chk(fd >= 0, "O_CREAT (no EXCL) on existing file reopens");
    if (fd >= 0) close(fd);

    (void)unlink(path);

    if (fail_cnt == 0)
        printf("o_excl_test: PASS (%d tests)\n", pass_cnt);
    else
        printf("o_excl_test: FAIL (%d/%d failed)\n", fail_cnt,
               pass_cnt + fail_cnt);
    return fail_cnt ? 1 : 0;
}
