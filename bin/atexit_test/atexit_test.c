/* atexit_test (T2.18): POSIX atexit semantics.
 *   1. Handlers run LIFO on explicit exit().
 *   2. _Exit / _exit skip atexit handlers.
 *   3. Falling off main (via the CRT) also fires handlers.
 *   4. Registering past the table limit returns nonzero. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static int fails = 0;
#define CHECK(cond, label) do { \
    if (!(cond)) { printf("FAIL: %s\n", label); fails++; } \
} while (0)

/* Handlers take no argument, so the write fd has to live in a global. */
static int wfd;
static void h_A(void) { write(wfd, "A", 1); }
static void h_B(void) { write(wfd, "B", 1); }
static void h_C(void) { write(wfd, "C", 1); }

static int drain(int rfd, char *buf, int buflen) {
    int n = 0, r;
    while (n < buflen - 1 && (r = read(rfd, buf + n, buflen - 1 - n)) > 0)
        n += r;
    buf[n] = '\0';
    return n;
}

static int reap(int pid) {
    int st = 0;
    while (1) {
        int got = wait(&st);
        if (got == pid) return st;
        if (got < 0 && errno != EINTR) return -1;
    }
}

/* Fork a child; run `body` inside it with wfd primed to the pipe's write
 * end. After body returns the child explicitly exit(0)s — so this fixture
 * tests the exit() path, not the main-return path. */
static void run_body(void (*body)(void), char *buf, int buflen) {
    int p[2];
    if (pipe(p) < 0) { printf("FAIL: pipe\n"); fails++; buf[0] = '\0'; return; }
    int pid = fork();
    if (pid == 0) {
        close(p[0]);
        wfd = p[1];
        body();
        exit(0);
    }
    close(p[1]);
    drain(p[0], buf, buflen);
    close(p[0]);
    reap(pid);
}

static void body_lifo(void) {
    atexit(h_A);
    atexit(h_B);
    atexit(h_C);
    /* falls through to exit(0) in run_body */
}

static void body_underscore_Exit(void) {
    atexit(h_A);
    _Exit(0);  /* must NOT run h_A */
}

static void body_underscore_exit(void) {
    atexit(h_A);
    _exit(0);  /* must NOT run h_A */
}

static void body_explicit_exit(void) {
    atexit(h_A);
    atexit(h_B);
    exit(0);  /* expect BA */
}

static void body_full(void) {
    int registered = 0;
    for (int i = 0; i < 32; i++) {
        if (atexit(h_A) == 0) registered++;
    }
    int extra = atexit(h_A);
    if (registered == 32 && extra != 0)
        write(wfd, "PASS", 4);
    else
        write(wfd, "FAIL", 4);
    _Exit(0);  /* don't pollute the fd with 32 'A's */
}

/* exec atexit_helper with the pipe write-end on fd 3. The helper's main
 * just registers a handler and returns 0; the handler writes "R" iff the
 * CRT runs atexit handlers when main returns. */
static void test_main_return(void) {
    int p[2];
    if (pipe(p) < 0) { printf("FAIL: pipe\n"); fails++; return; }
    int pid = fork();
    if (pid == 0) {
        close(p[0]);
        if (p[1] != 3) {
            dup2(p[1], 3);
            close(p[1]);
        }
        char *args[] = { "/bin/atexit_helper", 0 };
        execv("/bin/atexit_helper", args);
        _exit(127);
    }
    close(p[1]);
    char buf[8];
    drain(p[0], buf, sizeof(buf));
    close(p[0]);
    reap(pid);
    CHECK(strcmp(buf, "R") == 0, "main-return triggers atexit via CRT");
}

int main(void) {
    char buf[64];

    run_body(body_lifo, buf, sizeof(buf));
    CHECK(strcmp(buf, "CBA") == 0, "LIFO order on implicit exit");

    run_body(body_explicit_exit, buf, sizeof(buf));
    CHECK(strcmp(buf, "BA") == 0, "LIFO order on explicit exit");

    run_body(body_underscore_Exit, buf, sizeof(buf));
    CHECK(buf[0] == '\0', "_Exit skips atexit");

    run_body(body_underscore_exit, buf, sizeof(buf));
    CHECK(buf[0] == '\0', "_exit skips atexit");

    run_body(body_full, buf, sizeof(buf));
    CHECK(strcmp(buf, "PASS") == 0, "atexit refuses 33rd registration");

    test_main_return();

    if (fails == 0) printf("atexit_test: PASS\n");
    else printf("atexit_test: %d FAIL(s)\n", fails);
    return fails;
}
