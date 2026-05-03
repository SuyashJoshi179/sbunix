#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>

int main(void) {
    int pid = fork();
    if (pid == 0) {
        sleep_ms(150);
        _exit(7);
    }

    /* Immediately: child still running → WNOHANG returns 0. */
    int st = 0;
    int r = wait4(-1, &st, WNOHANG, 0);
    if (r != 0) {
        printf("FAIL  expected 0 got %d\n", r);
        return 1;
    }

    /* Wait long enough for child to exit. */
    sleep_ms(300);
    r = wait4(-1, &st, WNOHANG, 0);
    if (r != pid || !WIFEXITED(st) || WEXITSTATUS(st) != 7) {
        printf("FAIL  r=%d st=0x%x\n", r, st);
        return 1;
    }
    printf("wait4_nohang_test: ok\n");
    return 0;
}
