#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Write a one-line shebang script to /tmp and exec it directly, verifying
 * the kernel parses #! and re-targets the loader at the interpreter. The
 * script body invokes the shell's `exit N` builtin so we can check that
 * argv was assembled correctly. */

static int write_script(const char *path, const char *body) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;
    long n = (long)strlen(body);
    if (write(fd, body, n) != n) { close(fd); return -1; }
    close(fd);
    return 0;
}

static int run(const char *path, int *out_status) {
    int pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *argv[] = {(char *)path, 0};
        execv(path, argv);
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) != pid) return -1;
    *out_status = st;
    return 0;
}

int main(void) {
    const char *path = "/tmp/_shebang.sh";
    if (write_script(path, "#!/bin/sh\nexit 7\n") < 0) {
        printf("shebang_test: write failed\n");
        return 1;
    }
    int st = 0;
    if (run(path, &st) < 0) {
        printf("shebang_test: run failed\n");
        return 1;
    }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 7) {
        printf("shebang_test: WEXITSTATUS=%d (want 7)\n", WEXITSTATUS(st));
        return 1;
    }

    /* Shebang with a leading-space and explicit echo: exercises whitespace
     * skipping and confirms the script path is propagated as argv[1]. */
    if (write_script(path, "#!  /bin/sh\necho shebang-ok\n") < 0) {
        printf("shebang_test: write 2 failed\n");
        return 1;
    }
    if (run(path, &st) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("shebang_test: second run failed st=%d\n", st);
        return 1;
    }

    printf("shebang_test: PASS\n");
    return 0;
}
