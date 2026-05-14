/*
 * kill — send a signal to a process.
 *
 * Usage: kill [-N] pid...
 *
 * Defaults to SIGTERM (15) when no signal is given. Supports numeric
 * signal selection via -N (e.g. -9). Symbolic names like -SIGINT are
 * not parsed — keep it minimal. The shell has its own `kill` built-in
 * that does symbolic parsing; this binary is for /bin/kill paths.
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv) {
    int sig = SIGTERM;
    int i = 1;
    if (i < argc && argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9') {
        sig = atoi(argv[i] + 1);
        /* POSIX: signal 0 is the "is this pid alive?" probe — permit it.
         * Reject only negative or out-of-range values. */
        if (sig < 0 || sig >= 64) {
            fprintf(stderr, "kill: invalid signal '%s'\n", argv[i]);
            return 1;
        }
        i++;
    }
    if (i >= argc) {
        fprintf(stderr, "usage: kill [-sig] pid...\n");
        return 1;
    }
    int status = 0;
    for (; i < argc; i++) {
        int pid = atoi(argv[i]);
        if (kill(pid, sig) < 0) {
            fprintf(stderr, "kill: (%d): %s\n", pid, strerror(errno));
            status = 1;
        }
    }
    return status;
}
