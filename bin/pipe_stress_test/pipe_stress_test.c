#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[pipe_stress_test] PASS  %s\n", name);
    } else {
        printf("[pipe_stress_test] FAIL  %s\n", name);
        fails++;
    }
}

int main(void) {
    printf("=== pipe_stress_test ===\n");

    int fds[2];
    int rc = pipe(fds);
    check(rc == 0, "pipe() succeeds");

    int pid = fork();
    if (pid == 0) {
        close(fds[0]);
        char buf[64];
        for (int i = 0; i < 100; i++) {
            for (int j = 0; j < 64; j++)
                buf[j] = (char)(i + j);
            write(fds[1], buf, 64);
        }
        close(fds[1]);
        exit(0);
    }

    close(fds[1]);
    char rbuf[64];
    int total = 0;
    int data_ok = 1;
    int batch = 0;
    while (1) {
        long n = read(fds[0], rbuf, 64);
        if (n <= 0) break;
        for (int j = 0; j < n; j++) {
            if (rbuf[j] != (char)(batch + j))
                data_ok = 0;
        }
        total += n;
        if (n == 64) batch++;
    }
    close(fds[0]);
    int st;
    wait(&st);
    check(st == 0, "writer child exited cleanly");
    check(total == 6400, "received all 6400 bytes");
    check(data_ok, "data integrity across pipe");

    rc = pipe(fds);
    check(rc == 0, "second pipe() succeeds");
    pid = fork();
    if (pid == 0) {
        close(fds[1]);
        char c;
        long n = read(fds[0], &c, 1);
        check(n == 0 || n == -1, "child: read from closed pipe returns 0/-1");
        close(fds[0]);
        exit(0);
    }
    close(fds[0]);
    close(fds[1]);
    wait(&st);
    check(st == 0, "reader-of-closed-pipe child exited cleanly");

    int p1[2], p2[2];
    pipe(p1);
    pipe(p2);
    pid = fork();
    if (pid == 0) {
        close(p1[1]);
        close(p2[0]);
        char c;
        read(p1[0], &c, 1);
        c += 1;
        write(p2[1], &c, 1);
        close(p1[0]);
        close(p2[1]);
        exit(0);
    }
    close(p1[0]);
    close(p2[1]);
    char send = 41;
    write(p1[1], &send, 1);
    close(p1[1]);
    char recv;
    read(p2[0], &recv, 1);
    close(p2[0]);
    wait(&st);
    check(recv == 42, "bidirectional pipe: 41 -> child -> 42");
    check(st == 0, "bidirectional child exited cleanly");

    printf("=== pipe_stress_test: %d failures ===\n", fails);
    return fails;
}
