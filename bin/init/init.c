#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

int main(void) {
    while (1) {
        int pid = fork();
        if (pid == 0) {
            execv("/bin/sh", 0);
            printf("init: exec /bin/sh failed\n");
            exit(1);
        }
        while (1) {
            int got = wait(0);
            if (got == pid) break;
            if (got == -EINTR) continue;
            if (got < 0) break;
        }
    }
}
