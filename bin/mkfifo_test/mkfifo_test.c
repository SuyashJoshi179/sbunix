#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

/* SBUnix has no FIFO subsystem — mkfifo must return -1 with ENOSYS.
 * Regression guard, not a fix: a silent-success drift would let portable
 * code that depends on FIFO semantics misbehave invisibly. */
int main(void) {
    errno = 0;
    int r = mkfifo("/tmp/mkfifo_test.fifo", 0644);
    if (r != -1) {
        printf("FAIL: mkfifo returned %d (expected -1)\n", r);
        (void)unlink("/tmp/mkfifo_test.fifo");
        return 1;
    }
    if (errno != ENOSYS) {
        printf("FAIL: mkfifo errno=%d (expected ENOSYS=%d)\n", errno, ENOSYS);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
