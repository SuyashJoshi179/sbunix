#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>

/* Supervisor (the test process) stays in its own pgrp.  It forks 3
 * children that all join a new pgrp led by the first child.  The
 * supervisor then kill(-target_pgid, SIGTERM)s the whole pgrp from
 * outside it and reaps the bodies. */
int main(void) {
    int target_pgid = 0;
    for (int i = 0; i < 3; i++) {
        int pid = fork();
        if (pid == 0) {
            int leader = (target_pgid == 0) ? 0 : target_pgid;
            setpgid(0, leader);
            pause();
            _exit(99);
        }
        if (target_pgid == 0) target_pgid = pid;
    }
    /* Yield long enough for every child to run its setpgid + reach pause(). */
    sleep_ms(50);

    int rc = kill(-target_pgid, SIGTERM);
    int got = 0;
    for (int i = 0; i < 3; i++) {
        int st;
        int r = wait(&st);
        if (r > 0 && WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM) got++;
    }
    printf("kill_pgrp_test: rc=%d got=%d/3\n", rc, got);
    return (rc == 0 && got == 3) ? 0 : 1;
}
