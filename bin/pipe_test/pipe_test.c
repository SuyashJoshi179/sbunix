#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[pipe_test] PASS  %s\n", name);
    } else {
        printf("[pipe_test] FAIL  %s\n", name);
        fails++;
    }
}

static void test_basic(void) {
    int fds[2];
    int rc = pipe(fds);
    check(rc == 0, "pipe() returns 0");
    check(fds[0] >= 0 && fds[1] >= 0, "pipe fds are valid");

    const char *msg = "hello pipe";
    int len = 10;
    long w = write(fds[1], msg, len);
    check(w == len, "write to pipe");

    char buf[32];
    long r = read(fds[0], buf, sizeof(buf));
    check(r == len, "read from pipe returns correct count");
    check(strncmp(buf, msg, len) == 0, "pipe data matches");

    close(fds[0]);
    close(fds[1]);
}

static void test_fork_pipe(void) {
    int fds[2];
    pipe(fds);

    int pid = fork();
    if (pid == 0) {
        close(fds[0]);
        write(fds[1], "child", 5);
        close(fds[1]);
        exit(0);
    }

    close(fds[1]);
    char buf[32];
    long r = read(fds[0], buf, sizeof(buf));
    check(r == 5, "fork+pipe: read returns 5");
    check(strncmp(buf, "child", 5) == 0, "fork+pipe: data correct");
    close(fds[0]);
    wait(0);
}

static void test_eof(void) {
    int fds[2];
    pipe(fds);
    close(fds[1]);

    char buf[32];
    long r = read(fds[0], buf, sizeof(buf));
    check(r == 0, "pipe EOF: read returns 0 after writer closes");
    close(fds[0]);
}

static void test_argv(void) {
    int pid = fork();
    if (pid == 0) {
        char *args[] = { "echo", "hello", "world", 0 };
        execv("/bin/echo", args);
        exit(1);
    }
    int status;
    wait(&status);
    check(status == 0, "execv with argv: echo exits 0");
}

int main(void) {
    printf("=== pipe_test ===\n");
    test_basic();
    test_fork_pipe();
    test_eof();
    test_argv();
    printf("=== pipe_test: %d failures ===\n", fails);
    return fails;
}
