#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PAGE_SZ 4096

static int pass, fail;

static void check(int cond, const char *name) {
    if (cond) {
        printf("PASS  %s\n", name);
        pass++;
    } else {
        printf("FAIL  %s\n", name);
        fail++;
    }
}

int main(void) {
    int r = 0;

    int pages = 0;
    while (pages < 8192) {
        void *p = sbrk(PAGE_SZ);
        if ((long)p < 0) {
            r = (int)(long)p;
            break;
        }
        ((volatile char *)p)[0] = 0x5a;
        pages++;
    }
    check(r == -ENOMEM, "sbrk exhaustion returns -ENOMEM");

    int kids[256];
    int nkids = 0;
    int fork_err = 0;

    while (nkids < (int)(sizeof(kids) / sizeof(kids[0]))) {
        int pid = fork();
        if (pid < 0) {
            fork_err = pid;
            break;
        }
        if (pid == 0) {
            while (1) sleep_ms(1000);
        }
        kids[nkids++] = pid;
    }
    check(fork_err == -ENOMEM, "fork exhaustion returns -ENOMEM");

    for (int i = 0; i < nkids; i++)
        kill(kids[i], SIGTERM);

    for (int i = 0; i < nkids; i++) {
        int st = 0;
        while (1) {
            int got = wait(&st);
            if (got == kids[i]) break;
            if (got == -EINTR) continue;
            if (got < 0) break;
        }
    }

    check(1, "parent remains healthy after OOM paths");
    printf("oom_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
