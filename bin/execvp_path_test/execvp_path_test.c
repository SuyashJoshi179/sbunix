#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/wait.h>

/* execvp(2) must honor $PATH from the calling environment, and
 * execvpe(2) must honor PATH from its envp argument. Both should
 * fall back to /bin:/usr/bin when no PATH is set. */

static int run_child_execvp(const char *name) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *a[] = { (char *)name, "hi", 0 };
        execvp(name, a);
        _exit(99); /* exec failed */
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st)) return -2;
    return WEXITSTATUS(st);
}

static int run_child_execvpe(const char *name, char *const envp[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *a[] = { (char *)name, "hi", 0 };
        execvpe(name, a, envp);
        _exit(99);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st)) return -2;
    return WEXITSTATUS(st);
}

int main(void) {
    /* 1. Default PATH (whatever the runtime gave us) should find
     *    /bin/execvp_helper. */
    int rc = run_child_execvp("execvp_helper");
    if (rc != 42) {
        printf("FAIL: execvp default rc=%d\n", rc);
        return 1;
    }

    /* 2. PATH explicitly set to "/bin" finds it. */
    setenv("PATH", "/bin", 1);
    rc = run_child_execvp("execvp_helper");
    if (rc != 42) {
        printf("FAIL: execvp PATH=/bin rc=%d\n", rc);
        return 1;
    }

    /* 3. PATH set to a dead dir → exec fails (child exits 99). */
    setenv("PATH", "/nope/nowhere", 1);
    rc = run_child_execvp("execvp_helper");
    if (rc != 99) {
        printf("FAIL: execvp dead PATH rc=%d (want 99)\n", rc);
        return 1;
    }

    /* 4. Colon-separated: first dead, second hits /bin. */
    setenv("PATH", "/nope:/bin", 1);
    rc = run_child_execvp("execvp_helper");
    if (rc != 42) {
        printf("FAIL: execvp colon-PATH rc=%d\n", rc);
        return 1;
    }

    /* 5. PATH unset → default /bin fallback. */
    unsetenv("PATH");
    rc = run_child_execvp("execvp_helper");
    if (rc != 42) {
        printf("FAIL: execvp no-PATH rc=%d\n", rc);
        return 1;
    }

    /* 6. execvpe with a custom envp containing PATH=/bin. */
    char *envp_good[] = { "PATH=/bin", 0 };
    rc = run_child_execvpe("execvp_helper", envp_good);
    if (rc != 42) {
        printf("FAIL: execvpe PATH=/bin rc=%d\n", rc);
        return 1;
    }

    /* 7. execvpe with a custom envp containing PATH=/nowhere — fails. */
    char *envp_bad[] = { "PATH=/nowhere", 0 };
    rc = run_child_execvpe("execvp_helper", envp_bad);
    if (rc != 99) {
        printf("FAIL: execvpe dead PATH rc=%d (want 99)\n", rc);
        return 1;
    }

    /* 8. execvpe with empty envp (no PATH) → fallback default. */
    char *envp_empty[] = { 0 };
    rc = run_child_execvpe("execvp_helper", envp_empty);
    if (rc != 42) {
        printf("FAIL: execvpe empty envp rc=%d\n", rc);
        return 1;
    }

    /* 9. Slashed name bypasses PATH search. */
    setenv("PATH", "/nowhere", 1);
    rc = run_child_execvp("/bin/execvp_helper");
    if (rc != 42) {
        printf("FAIL: execvp slashed-name rc=%d\n", rc);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
