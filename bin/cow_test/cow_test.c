#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fails = 0;
static volatile int shared_var = 42;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[cow_test] PASS  %s\n", name);
    } else {
        printf("[cow_test] FAIL  %s\n", name);
        fails++;
    }
}

int main(void) {
    printf("=== cow_test ===\n");

    int pid = fork();
    if (pid == 0) {
        shared_var = 99;
        check(shared_var == 99, "child: write to COW page");
        exit(0);
    }

    int status;
    wait(&status);
    check(status == 0, "child exited cleanly");
    check(shared_var == 42, "parent: shared_var unchanged after child COW write");

    pid = fork();
    if (pid == 0) {
        check(shared_var == 42, "second child sees original value");
        shared_var = 77;
        check(shared_var == 77, "second child writes successfully");
        exit(0);
    }
    wait(&status);
    check(status == 0, "second child exited cleanly");
    check(shared_var == 42, "parent still sees 42 after second child");

    printf("=== cow_test: %d failures ===\n", fails);
    return fails;
}
