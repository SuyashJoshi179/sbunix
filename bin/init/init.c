#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>

int main(void) {
    printf("init: starting\n");

    /* Become session leader and claim the console. Children fork into
     * their own pgrps; the foreground pgrp is handed off via tcsetpgrp
     * so signal chars (^C/^Z/^\) only hit the running test. */
    setsid();
    int shell_pgid = getpgrp();
    ioctl(0, 0x5410 /* TIOCSPGRP */, &shell_pgid);

    while (1) {
        printf("Starting /bin/sh\n");
        int pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            execv("/bin/sh", 0);
            printf("init: exec /bin/sh failed\n");
            exit(1);
        }
        setpgid(pid, pid);
        ioctl(0, 0x5410 /* TIOCSPGRP */, &pid);
        while (1) {
            int got = wait(0);
            if (got == pid) break;
            if (got == -1 && errno == EINTR) continue;
            if (got < 0) break;
        }
        ioctl(0, 0x5410 /* TIOCSPGRP */, &shell_pgid);
    }
}
