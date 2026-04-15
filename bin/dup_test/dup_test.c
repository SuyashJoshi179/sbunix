// dup_test: verify dup() and dup2() file-descriptor semantics.
//
// POSIX contract under test:
//   - dup() returns the lowest available fd number.
//   - Duplicated fds share the same file description (offset advances through
//     either fd are visible to all).
//   - Closing one fd does not close the other.
//   - dup2(old, new) atomically closes 'new' (if open) and makes it refer to
//     'old'; old and new then share offset.
//   - dup2(fd, fd) is a documented no-op that returns fd.
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

static int pass_cnt = 0, fail_cnt = 0;

static void chk(int cond, const char *msg) {
    if (cond) { printf("[dup_test] PASS %s\n", msg); pass_cnt++; }
    else       { printf("[dup_test] FAIL %s\n", msg); fail_cnt++; }
}

int main(void) {
    // --- Part 1: dup() -------------------------------------------------------

    // fd 0/1/2 are stdin/stdout/stderr; first open gets fd 3.
    int fd1 = open("/etc/rc", O_RDONLY);
    chk(fd1 == 3, "open gives fd 3 (lowest free after stdio)");

    int fd2 = dup(fd1);
    chk(fd2 == 4, "dup gives fd 4 (next lowest)");

    // Both fds share offset.  Read 2 bytes via fd1 → offset becomes 2 for both.
    char buf[8];
    long n = read(fd1, buf, 2);
    chk(n == 2, "read 2 bytes via fd1");

    long pos = lseek(fd2, 0, 1 /* SEEK_CUR */);
    chk(pos == 2, "fd2 offset == 2 after reading via fd1 (shared)");

    // Read 2 more bytes via fd2 → offset becomes 4 for both.
    n = read(fd2, buf, 2);
    chk(n == 2, "read 2 bytes via fd2");

    pos = lseek(fd1, 0, 1 /* SEEK_CUR */);
    chk(pos == 4, "fd1 offset == 4 after reading via fd2 (shared)");

    // Closing fd1 must not destroy the underlying file description.
    close(fd1);
    n = read(fd2, buf, 1);
    chk(n >= 0, "fd2 still readable after fd1 closed");

    close(fd2);

    // --- Part 2: dup2() ------------------------------------------------------

    // Open two independent descriptions of the same file.
    int fd_a = open("/etc/rc", O_RDONLY);   // → fd 3 (reused)
    int fd_b = open("/etc/rc", O_RDONLY);   // → fd 4
    chk(fd_a >= 0 && fd_b >= 0, "open two independent fds");
    chk(fd_b == fd_a + 1, "consecutive fd allocation");

    // dup2(fd_a, fd_b): fd_b is replaced; both now share fd_a's description.
    int rc = dup2(fd_a, fd_b);
    chk(rc == fd_b, "dup2 returns newfd");

    // Advance offset via fd_a; fd_b must see the same position.
    read(fd_a, buf, 3);
    pos = lseek(fd_b, 0, 1 /* SEEK_CUR */);
    chk(pos == 3, "fd_b offset == 3 after reading via fd_a (dup2 shares)");

    // dup2(fd, fd) is a no-op; must return fd unchanged.
    rc = dup2(fd_a, fd_a);
    chk(rc == fd_a, "dup2(fd_a, fd_a) is a no-op returning fd_a");

    close(fd_a);
    close(fd_b);

    if (fail_cnt == 0)
        printf("dup_test: PASS (%d tests)\n", pass_cnt);
    else
        printf("dup_test: FAIL (%d/%d failed)\n", fail_cnt, pass_cnt + fail_cnt);
    return fail_cnt ? 1 : 0;
}
