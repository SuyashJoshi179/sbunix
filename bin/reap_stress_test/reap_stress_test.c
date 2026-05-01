#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define LOOPS 150

static int spawn_orphan_cycle(void) {
    int shell_like = fork();
    if (shell_like < 0) return shell_like;

    if (shell_like == 0) {
        int bg = fork();
        if (bg < 0) exit(2);
        if (bg == 0) {
            sleep_ms(20);
            exit(0);
        }
        exit(0);
    }

    int st = -1;
    while (1) {
        int got = wait(&st);
        if (got == shell_like) break;
        if (got == -1 && errno == EINTR) continue;
        if (got < 0) return got;
    }

    sleep_ms(40);
    return 0;
}

int main(void) {
    for (int i = 0; i < LOOPS; i++) {
        int rc = spawn_orphan_cycle();
        if (rc < 0) {
            printf("reap_stress_test: cycle %d failed (%d)\n", i, rc);
            return 1;
        }

        int st;
        int w = wait(&st);
        if (w != -1 || errno != ECHILD) {
            printf("reap_stress_test: cycle %d expected ECHILD got w=%d errno=%d\n", i, w, errno);
            return 1;
        }
    }

    printf("reap_stress_test: PASS (%d cycles)\n", LOOPS);
    return 0;
}
