#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    int shell_like = fork();
    if (shell_like < 0) {
        printf("orphan_test: fork shell-like failed\n");
        return 1;
    }

    if (shell_like == 0) {
        int bg = fork();
        if (bg < 0) exit(2);
        if (bg == 0) {
            sleep_ms(300);
            exit(0);
        }
        exit(0);
    }

    int st = 0;
    while (1) {
        int got = wait(&st);
        if (got == shell_like) break;
        if (got == -EINTR) continue;
        if (got < 0) {
            printf("orphan_test: wait shell-like failed (%d)\n", got);
            return 1;
        }
    }

    sleep_ms(600);

    int rc = wait(&st);
    if (rc != -ECHILD) {
        printf("orphan_test: expected -ECHILD after reparent, got %d\n", rc);
        return 1;
    }

    printf("orphan_test: PASS\n");
    return 0;
}
