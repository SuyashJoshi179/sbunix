#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

int main(void) {
    printf("init: starting\n");

    /* Become session leader and claim the console. Foreground pgrp is
     * handed off to the running shell via tcsetpgrp so signal chars
     * (^C/^Z/^\) only hit the foreground job. */
    setsid();
    int shell_pgid = getpgrp();
    ioctl(0, 0x5410 /* TIOCSPGRP */, &shell_pgid);

    /* Run /etc/rc once at boot. */
    {
        int rc_pid = fork();
        if (rc_pid == 0) {
            char *args[] = {"/bin/sh", "/etc/rc", 0};
            execv("/bin/sh", args);
            printf("init: exec /bin/sh /etc/rc failed\n");
            exit(1);
        }
        if (rc_pid > 0) {
            int rc_st;
            while (1) {
                int got = wait(&rc_st);
                if (got == rc_pid) break;
                if (got < 0 && errno != EINTR) break;
            }
        }
    }

    int sh_failures = 0;
    while (1) {
        /* Reap any orphans whose original parents have already exited.
         * Without this, detached subprocesses become permanent zombies
         * because nobody else calls wait() on them. */
        while (waitpid(-1, 0, WNOHANG) > 0) { }

        printf("Starting /bin/sh\n");
        int pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            char *sh_args[] = { "/bin/sh", 0 };
            execv("/bin/sh", sh_args);
            printf("init: exec /bin/sh failed\n");
            exit(127);
        }
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

        /* Backoff: a shell that exec-fails or crashes within ~1 tick
         * means there's no recoverable shell to spawn. Without backoff
         * init busy-loops forking and the kernel log fills with
         * "Starting /bin/sh ... init: exec /bin/sh failed" instantly. */
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
