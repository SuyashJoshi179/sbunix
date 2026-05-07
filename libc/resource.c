#include <sys/resource.h>
#include <string.h>
#include <errno.h>

/* getrlimit/setrlimit are wired to kernel syscalls in syscall.c.
 * getrusage/getpriority/setpriority remain stubs. */

int getrusage(int who, struct rusage *usage) {
    (void)who;
    if (!usage) { errno = EFAULT; return -1; }
    memset(usage, 0, sizeof(*usage));
    return 0;
}

int getpriority(int which, id_t who) {
    (void)which; (void)who;
    return 0;
}

int setpriority(int which, id_t who, int prio) {
    (void)which; (void)who; (void)prio;
    return 0;
}
