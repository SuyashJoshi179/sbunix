#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

/* init is the first user process. Becomes session leader, claims the
 * console, runs /etc/rc once (which mounts filesystems, invokes
 * /bin/runtests, and exec's an interactive /bin/sh), then re-spawns
 * plain /bin/sh whenever the interactive shell exits.
 *
 * The test loop lives in /bin/runtests rather than here so /etc/rc can
 * keep its conventional `exec /bin/sh` tail without blocking the test
 * runner — see commit message of the rc/runtests refactor for the full
 * rationale. */
int main(void) {
    printf("init: starting\n");

    setsid();
    int shell_pgid = getpgrp();
    ioctl(0, 0x5410 /* TIOCSPGRP */, &shell_pgid);

    int first = 1;
    int sh_failures = 0;
    while (1) {
        while (waitpid(-1, 0, WNOHANG) > 0) { }

        printf("Starting /bin/sh\n");
        int pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            if (first) {
                char *sh_args[] = { "/bin/sh", "/etc/rc", 0 };
                execv("/bin/sh", sh_args);
            } else {
                char *sh_args[] = { "/bin/sh", 0 };
                execv("/bin/sh", sh_args);
            }
            printf("init: exec /bin/sh failed\n");
            exit(127);
        }
        first = 0;
        setpgid(pid, pid);
        ioctl(0, 0x5410 /* TIOCSPGRP */, &pid);
        int sh_status = 0;
        while (1) {
            int got = wait(&sh_status);
            if (got == pid) break;
            if (got == -1 && errno == EINTR) continue;
            if (got < 0) break;
        }
        ioctl(0, 0x5410 /* TIOCSPGRP */, &shell_pgid);

        if (sh_status == 127) {
            sh_failures++;
            if (sh_failures >= 5) {
                printf("init: /bin/sh fails repeatedly; sleeping 5s\n");
                sleep(5);
                sh_failures = 0;
            }
        } else {
            sh_failures = 0;
        }
    }
}
