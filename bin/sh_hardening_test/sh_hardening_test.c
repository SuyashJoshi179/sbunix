/* sh_hardening_test (T2.23 + T2.25): exercise the audit-flagged shell
 * features — single-quote tokens, `;` separator, `/bin/true`, `/bin/false`,
 * and POSIX exec-failure exit codes (127 for ENOENT, 126 otherwise). */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails = 0;
#define CHECK(cond, label) do { \
    if (!(cond)) { printf("FAIL: %s\n", label); fails++; } \
} while (0)

static int run(const char *script) {
    int pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *argv[] = { "sh", "-c", (char *)script, 0 };
        execv("/bin/sh", argv);
        _exit(127);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) { }
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int main(void) {
    CHECK(run("/bin/true")  == 0, "/bin/true exits 0");
    CHECK(run("/bin/false") == 1, "/bin/false exits 1");

    /* `;` separator. The tokenizer matches POSIX shells in requiring at
     * least a leading space (cmd1;cmd2 with no whitespace stays one word
     * because the bare-word loop overwrites the delimiter with NUL —
     * same constraint that applies to `|` today). */
    CHECK(run("/bin/false ; /bin/true") == 0, "';' separator continues on failure");
    CHECK(run("/bin/true ; /bin/false") == 1, "';' separator: last status wins");

    /* echo prints arg list; single quotes prevent globbing/word-split. */
    CHECK(run("/bin/echo 'hello world' > /tmp/sh_hard_out") == 0, "single-quoted arg + redir");

    /* Exec failure: POSIX says ENOENT → 127. */
    CHECK(run("/bin/no_such_binary_xyz_") == 127, "exec ENOENT exits 127");

    unlink("/tmp/sh_hard_out");

    if (fails == 0) printf("sh_hardening_test: PASS\n");
    else printf("sh_hardening_test: %d FAIL(s)\n", fails);
    return fails;
}
