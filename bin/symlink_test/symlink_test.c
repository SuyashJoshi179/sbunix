#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;
static void check(int cond, const char *name) {
    if (cond) printf("[symlink_test] PASS  %s\n", name);
    else    { printf("[symlink_test] FAIL  %s\n", name); fails++; }
}

int main(void) {
    printf("=== symlink_test ===\n");

    /* readlink on /dev/loop returns the literal target. */
    char buf[64] = {0};
    long n = readlink("/dev/loop", buf, sizeof(buf) - 1);
    check(n == 9, "readlink length == 9");
    if (n > 0) buf[n] = 0;
    check(strcmp(buf, "/dev/loop") == 0, "readlink target string");

    /* readlink on a non-symlink returns -1, errno=EINVAL. */
    long r = readlink("/dev/console", buf, sizeof(buf));
    check(r == -1 && errno == EINVAL, "readlink on non-symlink → EINVAL");

    /* readlink on a missing path returns -1, errno=ENOENT. */
    r = readlink("/dev/no_such_thing", buf, sizeof(buf));
    check(r == -1 && errno == ENOENT, "readlink on missing path → ENOENT");

    /* Buffer truncation: target is 9 bytes, ask for 4 → 4 bytes, no NUL. */
    char tbuf[16];
    for (int i = 0; i < (int)sizeof(tbuf); i++) tbuf[i] = (char)0xAA;
    r = readlink("/dev/loop", tbuf, 4);
    check(r == 4, "truncated readlink returns 4");
    check(tbuf[4] == (char)0xAA, "truncated readlink writes no NUL");

    /* lstat reports S_IFLNK on the link itself. */
    struct stat st;
    int rc = lstat("/dev/loop", &st);
    check(rc == 0, "lstat /dev/loop succeeds");
    check(S_ISLNK(st.st_mode), "lstat /dev/loop is S_ISLNK");

    /* open follows the symlink → ELOOP because target is itself. */
    int fd = open("/dev/loop", 0);
    check(fd == -1 && errno == ELOOP, "open(/dev/loop) → ELOOP");

    /* lstat on a non-link still works (regression check). */
    rc = lstat("/dev/console", &st);
    check(rc == 0, "lstat /dev/console succeeds");
    check(S_ISCHR(st.st_mode), "lstat /dev/console is S_ISCHR");

    printf("=== symlink_test: %d failures ===\n", fails);
    return fails;
}
