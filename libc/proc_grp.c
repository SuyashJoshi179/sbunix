#include <unistd.h>
#include <errno.h>

/* SBUnix has no kernel-side process groups, sessions, or job control. We
 * pretend each process is its own group leader (pgid == sid == pid) so
 * shells that *check* job-control state without actively managing it
 * (e.g. mostly-functional bash ports) keep working. Mutating calls
 * succeed silently — pretending compliance is less surprising for ports
 * than failing with EPERM, which they often handle by aborting. */

pid_t getpgrp(void) { return getpid(); }

pid_t getpgid(pid_t pid) {
    if (pid == 0) return getpid();
    /* No way to look up another process's pgid; return its own pid as a
     * best-effort answer rather than EPERM. */
    return pid;
}

int setpgid(pid_t pid, pid_t pgid) {
    (void)pid; (void)pgid;
    return 0;
}

int setpgrp(void) { return 0; }

pid_t setsid(void) { return getpid(); }

pid_t getsid(pid_t pid) {
    if (pid == 0) return getpid();
    return pid;
}

pid_t tcgetsid(int fd) {
    (void)fd;
    return getpid();
}
