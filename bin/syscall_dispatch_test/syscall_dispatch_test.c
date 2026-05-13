#include <errno.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Round-trip the generic syscall(2) dispatcher against a couple of known
 * syscall numbers. If the SYS_* macros, the va_list plumbing, or the
 * errno translation drift apart, this fails at boot. */

int main(void) {
    /* SYS_getpid takes no args; result must equal what the wrapper returns. */
    long via_dispatch = syscall(SYS_getpid);
    int  via_wrapper  = getpid();
    if (via_dispatch != via_wrapper || via_dispatch <= 0) {
        printf("syscall_dispatch_test: getpid mismatch (%ld vs %d)\n",
               via_dispatch, via_wrapper);
        return 1;
    }

    /* SYS_open on a missing path must surface ENOENT via errno, with -1 return. */
    errno = 0;
    long rc = syscall(SYS_open, (long)"/no/such/path/xyz_", 0L);
    if (rc != -1 || errno != ENOENT) {
        printf("syscall_dispatch_test: open(-1/ENOENT) got rc=%ld errno=%d\n",
               rc, errno);
        return 1;
    }

    printf("syscall_dispatch_test: PASS\n");
    return 0;
}
