/* env_test — exercise getenv / setenv / unsetenv / putenv with shared
 * backing storage. POSIX semantics; the kernel does not propagate envp
 * across execv, so the environment starts empty in every fresh process. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

static int fails = 0;

static void ok(int cond, const char *name) {
    if (cond) printf("[env_test] PASS  %s\n", name);
    else      { printf("[env_test] FAIL  %s\n", name); fails++; }
}

int main(void) {
    /* (1) getenv on a fresh process returns NULL — environment is empty
     *     because kernel SYS_execv does not pass envp. */
    ok(getenv("HOME") == NULL, "fresh process: getenv returns NULL");

    /* (2) setenv installs a value visible via getenv. */
    ok(setenv("FOO", "bar", 1) == 0, "setenv FOO=bar returns 0");
    {
        char *v = getenv("FOO");
        ok(v && strcmp(v, "bar") == 0, "getenv FOO returns 'bar'");
    }

    /* (3) setenv with overwrite=0 keeps the existing value. */
    ok(setenv("FOO", "newval", 0) == 0, "setenv FOO overwrite=0 returns 0");
    {
        char *v = getenv("FOO");
        ok(v && strcmp(v, "bar") == 0, "FOO unchanged when overwrite=0");
    }

    /* (4) setenv with overwrite=1 replaces. */
    ok(setenv("FOO", "newval", 1) == 0, "setenv FOO overwrite=1 returns 0");
    {
        char *v = getenv("FOO");
        ok(v && strcmp(v, "newval") == 0, "FOO replaced under overwrite=1");
    }

    /* (5) unsetenv removes; getenv returns NULL. */
    ok(unsetenv("FOO") == 0, "unsetenv FOO returns 0");
    ok(getenv("FOO") == NULL, "FOO gone after unsetenv");

    /* (6) Argument validation. */
    errno = 0;
    ok(setenv(NULL, "v", 1) == -1 && errno == EINVAL,
       "setenv(NULL,...) → -1 EINVAL");
    errno = 0;
    ok(setenv("", "v", 1) == -1 && errno == EINVAL,
       "setenv(\"\",...) → -1 EINVAL");
    errno = 0;
    ok(setenv("A=B", "v", 1) == -1 && errno == EINVAL,
       "setenv name with '=' → -1 EINVAL");
    errno = 0;
    ok(unsetenv("X=Y") == -1 && errno == EINVAL,
       "unsetenv name with '=' → -1 EINVAL");

    /* (7) putenv installs the caller's pointer directly (POSIX). */
    static char buf[] = "PUT=one";
    ok(putenv(buf) == 0, "putenv PUT=one returns 0");
    {
        char *v = getenv("PUT");
        ok(v && strcmp(v, "one") == 0, "getenv PUT returns 'one'");
        /* Mutating the buffer must change what getenv sees, since putenv
         * keeps the caller's pointer rather than copying. */
        buf[4] = 't'; buf[5] = 'w'; buf[6] = 'o';
        v = getenv("PUT");
        ok(v && strcmp(v, "two") == 0, "putenv pointer is live (no copy)");
    }

    /* (8) putenv("NAME") with no '=' removes (glibc extension). */
    {
        static char put_remove[] = "PUT";
        ok(putenv(put_remove) == 0, "putenv PUT (no '=') returns 0");
        ok(getenv("PUT") == NULL, "PUT removed after putenv with no '='");
    }

    /* (9) Cross-consistency: putenv then setenv overwrites; both reachable
     *     through the shared environ. */
    {
        static char ent[] = "MIX=p";
        ok(putenv(ent) == 0, "putenv MIX=p");
        ok(setenv("MIX", "s", 1) == 0, "setenv MIX=s overwrite=1");
        char *v = getenv("MIX");
        ok(v && strcmp(v, "s") == 0, "MIX reflects setenv");
    }

    /* (10) Growth: install many entries; environ should still be a valid
     *      NULL-terminated array of all of them. */
    {
        char name[16];
        for (int i = 0; i < 32; i++) {
            snprintf(name, sizeof(name), "K%d", i);
            char val[8]; snprintf(val, sizeof(val), "v%d", i);
            if (setenv(name, val, 1) != 0) { ok(0, "setenv K_i in loop"); break; }
        }
        int all_present = 1;
        for (int i = 0; i < 32; i++) {
            snprintf(name, sizeof(name), "K%d", i);
            char *v = getenv(name);
            char expect[8]; snprintf(expect, sizeof(expect), "v%d", i);
            if (!v || strcmp(v, expect) != 0) { all_present = 0; break; }
        }
        ok(all_present, "32 setenv entries all readable");
    }

    /* (11) environ pointer must be non-NULL after mutations and the array
     *      must be NULL-terminated. */
    {
        int saw_null = 0;
        for (int i = 0; i < 256; i++) {
            if (environ[i] == NULL) { saw_null = 1; break; }
        }
        ok(saw_null, "environ array is NULL-terminated");
    }

    printf("=== env_test: %d failures ===\n", fails);
    return fails ? 1 : 0;
}
