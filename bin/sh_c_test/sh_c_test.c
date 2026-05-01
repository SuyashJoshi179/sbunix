#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static int run(const char *script, int *out_status) {
    int pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *argv[] = {"sh", "-c", (char *)script, 0};
        execv("/bin/sh", argv);
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) != pid) return -1;
    *out_status = st;
    return 0;
}

int main(void) {
    int st = 0;

    if (run("exit 5", &st) < 0) { printf("sh_c_test: run failed\n"); return 1; }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 5) {
        printf("sh_c_test: exit 5 -> WEXITSTATUS=%d (want 5)\n", WEXITSTATUS(st));
        return 1;
    }

    if (run("exit 0", &st) < 0) { printf("sh_c_test: run failed\n"); return 1; }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("sh_c_test: exit 0 -> WEXITSTATUS=%d (want 0)\n", WEXITSTATUS(st));
        return 1;
    }

    if (run("echo hello-from-dashc", &st) < 0) { printf("sh_c_test: run failed\n"); return 1; }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("sh_c_test: echo -> WEXITSTATUS=%d (want 0)\n", WEXITSTATUS(st));
        return 1;
    }

    /* External command: /bin/false would be ideal but may not exist;
     * use /bin/sh -c "exit 3" indirectly via /bin/echo failing path.
     * Instead, re-exec ourselves through the shell with a short script. */
    if (run("/bin/echo external-ok", &st) < 0) { printf("sh_c_test: run failed\n"); return 1; }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("sh_c_test: external echo -> WEXITSTATUS=%d (want 0)\n", WEXITSTATUS(st));
        return 1;
    }

    printf("sh_c_test: PASS\n");
    return 0;
}
