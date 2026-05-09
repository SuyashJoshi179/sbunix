/*
 * grader_cow_test — Copy-on-write correctness after fork.
 *
 * When a process forks, writes to inherited memory must trigger COW,
 * not crash. Parent's data must remain unchanged.
 * (Based on Eval 06.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[grader_cow_test] PASS  %s\n", name);
    } else {
        printf("[grader_cow_test] FAIL  %s\n", name);
        fails++;
    }
}

int main(void) {
    printf("=== grader_cow_test ===\n");

    /* 1. Heap buffer COW */
    char *buf = (char *)malloc(4096);
    check(buf != 0, "malloc(4096) for COW test");
    if (buf) {
        memset(buf, 'A', 4096);

        int pid = fork();
        if (pid == 0) {
            /* Child writes to inherited buffer — must NOT crash */
            memset(buf, 'B', 4096);
            int ok = (buf[0] == 'B' && buf[4095] == 'B');
            free(buf);
            exit(ok ? 0 : 1);
        }
        int st;
        waitpid(pid, &st, 0);
        check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
              "child writes to inherited heap buffer (no crash)");
        check(buf[0] == 'A' && buf[4095] == 'A',
              "parent heap buffer unchanged after child write");
        free(buf);
    }

    /* 2. Stack variable COW */
    volatile int stack_var = 42;
    {
        int pid = fork();
        if (pid == 0) {
            stack_var = 99;
            exit(stack_var == 99 ? 0 : 1);
        }
        int st;
        waitpid(pid, &st, 0);
        check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
              "child modifies stack variable (no crash)");
        check(stack_var == 42,
              "parent stack variable unchanged after child write");
    }

    /* 3. Large buffer COW — 1 MB */
    {
        char *big = (char *)malloc(1024 * 1024);
        check(big != 0, "malloc(1MB) for large COW test");
        if (big) {
            memset(big, 'X', 1024 * 1024);

            int pid = fork();
            if (pid == 0) {
                /* Child overwrites entire 1 MB buffer */
                memset(big, 'Y', 1024 * 1024);
                int ok = (big[0] == 'Y' && big[1024*1024-1] == 'Y');
                free(big);
                exit(ok ? 0 : 1);
            }
            int st;
            waitpid(pid, &st, 0);
            check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
                  "child overwrites 1MB inherited buffer (no crash)");
            check(big[0] == 'X' && big[1024*1024-1] == 'X',
                  "parent 1MB buffer unchanged");
            free(big);
        }
    }

    /* 4. Nested fork COW — grandchild writes to inherited buffer */
    {
        char *nested = (char *)malloc(4096);
        check(nested != 0, "malloc for nested COW");
        if (nested) {
            memset(nested, 'N', 4096);

            int pid = fork();
            if (pid == 0) {
                /* Child: modify buffer, then fork grandchild */
                nested[0] = 'C';
                int gpid = fork();
                if (gpid == 0) {
                    /* Grandchild writes */
                    nested[0] = 'G';
                    exit(nested[0] == 'G' ? 0 : 1);
                }
                int gst;
                waitpid(gpid, &gst, 0);
                int gc_ok = WIFEXITED(gst) && WEXITSTATUS(gst) == 0;
                int c_ok = (nested[0] == 'C');
                exit((gc_ok && c_ok) ? 0 : 1);
            }
            int st;
            waitpid(pid, &st, 0);
            check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
                  "nested fork: grandchild COW write succeeds");
            check(nested[0] == 'N',
                  "parent unchanged after nested fork writes");
            free(nested);
        }
    }

    /* 5. COW with multiple children writing same inherited buffer */
    {
        char *shared = (char *)malloc(4096);
        if (shared) {
            memset(shared, 'S', 4096);
            int all_ok = 1;
            for (int i = 0; i < 4; i++) {
                int pid = fork();
                if (pid == 0) {
                    shared[0] = (char)('0' + i);
                    exit(shared[0] == (char)('0' + i) ? 0 : 1);
                }
                int st;
                waitpid(pid, &st, 0);
                if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
                    all_ok = 0;
            }
            check(all_ok, "4 children each COW-write same buffer");
            check(shared[0] == 'S', "parent buffer intact after 4 COW children");
            free(shared);
        }
    }

    printf("=== grader_cow_test: %d failures ===\n", fails);
    return fails;
}
