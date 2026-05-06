/*
 * grader_system_test — Tests system(), getenv/setenv, access(), environ.
 *
 * These are the functions that block MicroPython and BusyBox from running.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

static int fails = 0;
static void check(int cond, const char *name) {
    if (cond) printf("[grader_system_test] PASS  %s\n", name);
    else { printf("[grader_system_test] FAIL  %s\n", name); fails++; }
}

int main(void) {
    printf("=== grader_system_test ===\n");

    /* --- system() --- */
    {
        int rc = system("echo system_works");
        check(rc == 0, "system('echo ...') returns 0");
    }
    {
        int rc = system("exit 42");
        int code = WIFEXITED(rc) ? WEXITSTATUS(rc) : rc;
        /* system() should return the wait status of sh -c */
        check(code == 42 || rc == 42 || rc == (42 << 8),
              "system('exit 42') propagates exit code");
    }
    {
        int rc = system(0);
        /* POSIX: system(NULL) returns non-zero if a shell is available */
        check(rc != 0, "system(NULL) returns non-zero (shell available)");
    }

    /* --- getenv / setenv --- */
    {
        int rc = setenv("GRADER_VAR", "hello", 1);
        check(rc == 0, "setenv succeeds");

        char *v = getenv("GRADER_VAR");
        check(v != 0, "getenv returns non-NULL after setenv");
        if (v) check(strcmp(v, "hello") == 0, "getenv returns correct value");
    }
    {
        /* overwrite=0 should not replace existing */
        setenv("GRADER_VAR", "hello", 1);
        setenv("GRADER_VAR", "world", 0);
        char *v = getenv("GRADER_VAR");
        if (v) check(strcmp(v, "hello") == 0,
                      "setenv overwrite=0 preserves existing");
    }
    {
        /* overwrite=1 should replace */
        setenv("GRADER_VAR", "first", 1);
        setenv("GRADER_VAR", "second", 1);
        char *v = getenv("GRADER_VAR");
        if (v) check(strcmp(v, "second") == 0,
                      "setenv overwrite=1 replaces value");
    }
    {
        setenv("GRADER_DEL", "temp", 1);
        unsetenv("GRADER_DEL");
        check(getenv("GRADER_DEL") == 0,
              "unsetenv removes variable");
    }
    {
        check(getenv("NONEXISTENT_GRADER_VAR_XYZ") == 0,
              "getenv(nonexistent) returns NULL");
    }

    /* --- environ --- */
    {
        extern char **environ;
        check(environ != 0, "environ is non-NULL");
        /* Should have at least PATH */
        char *path = getenv("PATH");
        if (path) {
            printf("  PATH=%s\n", path);
            check(1, "PATH environment variable exists");
        } else {
            printf("[grader_system_test] NOTE  PATH not set in environ\n");
        }
    }

    /* --- access() --- */
    {
        int rc = access("/bin/sh", F_OK);
        check(rc == 0, "access(/bin/sh, F_OK) succeeds");
    }
    {
        int rc = access("/bin/echo", F_OK);
        check(rc == 0, "access(/bin/echo, F_OK) succeeds");
    }
    {
        int rc = access("/nonexistent_grader_path", F_OK);
        check(rc != 0, "access(nonexistent) fails");
    }
    {
        int rc = access("/bin/sh", X_OK);
        check(rc == 0, "access(/bin/sh, X_OK) succeeds");
    }

    /* --- atexit --- */
    /* Can't easily test atexit inline (handler runs at exit),
     * but verify the function exists and doesn't crash */
    {
        int rc = atexit(0);
        /* atexit(NULL) may or may not be an error — just don't crash */
        (void)rc;
        check(1, "atexit(NULL) does not crash");
    }

    printf("=== grader_system_test: %d failures ===\n", fails);
    return fails;
}
