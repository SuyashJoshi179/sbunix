#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[exec_reset_test] PASS  %s\n", name);
    } else {
        printf("[exec_reset_test] FAIL  %s\n", name);
        fails++;
    }
}

int main(void) {
    printf("=== exec_reset_test ===\n");

    int pid = fork();
    if (pid == 0) {
        void *brk1 = sbrk(0);
        sbrk(4096 * 10);
        void *brk2 = sbrk(0);
        if ((char *)brk2 != (char *)brk1 + 4096 * 10) {
            printf("[exec_reset_test] FAIL  pre-exec sbrk\n");
            exit(1);
        }
        char *argv[] = {"/bin/echo", "exec_reset_ok", 0};
        execv("/bin/echo", argv);
        printf("[exec_reset_test] FAIL  exec failed\n");
        exit(1);
    }
    int st;
    wait(&st);
    check(st == 0, "child exec after sbrk growth succeeded");

    pid = fork();
    if (pid == 0) {
        void *p = sbrk(4096);
        volatile char *cp = (volatile char *)p;
        cp[0] = 'Z';
        check(cp[0] == 'Z', "post-exec-parent: sbrk works normally");
        exit(0);
    }
    wait(&st);
    check(st == 0, "parent sbrk works after child exec");

    pid = fork();
    if (pid == 0) {
        int pid2 = fork();
        if (pid2 == 0) {
            sbrk(4096 * 5);
            char *argv[] = {"/bin/echo", "nested_exec_ok", 0};
            execv("/bin/echo", argv);
            exit(1);
        }
        wait(&st);
        check(st == 0, "grandchild exec succeeded");
        exit(0);
    }
    wait(&st);
    check(st == 0, "nested fork+exec succeeded");

    printf("=== exec_reset_test: %d failures ===\n", fails);
    return fails;
}
