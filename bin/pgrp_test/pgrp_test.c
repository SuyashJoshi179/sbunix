#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>

static int pass, fail;
static void check(int c, const char *n) {
    if (c) { printf("PASS  %s\n", n); pass++; }
    else   { printf("FAIL  %s\n", n); fail++; }
}

int main(void) {
    check(getpgrp() > 0,                            "getpgrp() positive");
    check(setpgid(0, 0) == 0,                       "setpgid(0,0) ok");
    check(getpgrp() == getpid(),                    "self is pgrp leader");
    check(getpgid(0) == getpid(),                   "getpgid(0) == pid");
    check(getsid(0) > 0,                            "getsid(0) positive");

    int kid = fork();
    if (kid == 0) {
        setpgid(0, 0);
        if (getpgrp() == getpid()) _exit(0);
        _exit(1);
    }
    int st;
    while (wait(&st) != kid) {}
    check(WIFEXITED(st) && WEXITSTATUS(st) == 0,    "child setpgid(0,0) self-leader");

    printf("pgrp_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
