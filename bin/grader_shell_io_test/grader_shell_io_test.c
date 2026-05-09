/*
 * grader_shell_io_test — Shell I/O redirection and pipe integration.
 *
 * Professor runs commands through sh -c with redirections.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

static int fails = 0;
static void check(int cond, const char *name) {
    if (cond) printf("[grader_shell_io_test] PASS  %s\n", name);
    else { printf("[grader_shell_io_test] FAIL  %s\n", name); fails++; }
}

/* Run sh -c "cmd" and return WEXITSTATUS */
static int sh_c(const char *cmd) {
    int pid = fork();
    if (pid == 0) {
        char *args[] = {"/bin/sh", "-c", (char *)cmd, 0};
        execv("/bin/sh", args);
        exit(127);
    }
    int st;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int main(void) {
    printf("=== grader_shell_io_test ===\n");

    /* 1. Basic shell execution */
    check(sh_c("echo hello") == 0, "sh -c 'echo hello' exits 0");

    /* 2. Shell exit code propagation */
    check(sh_c("exit 0") == 0, "sh -c 'exit 0'");
    check(sh_c("exit 1") == 1, "sh -c 'exit 1'");
    check(sh_c("exit 42") == 42, "sh -c 'exit 42'");

    /* 3. Output redirection: echo > file */
    {
        sh_c("echo redirect_test > /mnt/tmp_shell_out");
        int fd = open("/mnt/tmp_shell_out", O_RDONLY);
        check(fd >= 0, "sh -c 'echo > file' creates file");
        if (fd >= 0) {
            char buf[64] = {0};
            read(fd, buf, sizeof(buf) - 1);
            close(fd);
            /* Trim trailing newline */
            char *nl = strchr(buf, '\n');
            if (nl) *nl = 0;
            check(strcmp(buf, "redirect_test") == 0,
                  "redirected output content matches");
        }
        unlink("/mnt/tmp_shell_out");
    }

    /* 4. Append redirection: >> */
    {
        sh_c("echo line1 > /mnt/tmp_shell_app");
        sh_c("echo line2 >> /mnt/tmp_shell_app");
        int fd = open("/mnt/tmp_shell_app", O_RDONLY);
        check(fd >= 0, "append creates file");
        if (fd >= 0) {
            char buf[128] = {0};
            read(fd, buf, sizeof(buf) - 1);
            close(fd);
            check(strstr(buf, "line1") != 0 && strstr(buf, "line2") != 0,
                  "append has both lines");
        }
        unlink("/mnt/tmp_shell_app");
    }

    /* 5. Input redirection: < file */
    {
        /* Create a file, then cat < file */
        int fd = open("/mnt/tmp_shell_in", O_CREAT | O_WRONLY);
        if (fd >= 0) {
            write(fd, "input_data\n", 11);
            close(fd);
        }
        int rc = sh_c("cat < /mnt/tmp_shell_in > /mnt/tmp_shell_in_out");
        if (rc == 0) {
            fd = open("/mnt/tmp_shell_in_out", O_RDONLY);
            if (fd >= 0) {
                char buf[64] = {0};
                read(fd, buf, sizeof(buf) - 1);
                close(fd);
                check(strstr(buf, "input_data") != 0,
                      "input redirection passes data to command");
            }
            unlink("/mnt/tmp_shell_in_out");
        } else {
            check(0, "input redirection command failed");
        }
        unlink("/mnt/tmp_shell_in");
    }

    /* 6. Pipe through shell */
    {
        int rc = sh_c("echo pipe_test | cat > /mnt/tmp_shell_pipe");
        if (rc == 0) {
            int fd = open("/mnt/tmp_shell_pipe", O_RDONLY);
            if (fd >= 0) {
                char buf[64] = {0};
                read(fd, buf, sizeof(buf) - 1);
                close(fd);
                check(strstr(buf, "pipe_test") != 0,
                      "shell pipe delivers data");
            }
            unlink("/mnt/tmp_shell_pipe");
        } else {
            check(0, "shell pipe command failed");
        }
    }

    /* 7. Command chaining with && */
    check(sh_c("echo a && echo b") == 0,
          "sh -c 'cmd1 && cmd2' exits 0");

    /* 8. /bin/sh exists and is executable */
    check(access("/bin/sh", F_OK) == 0 || open("/bin/sh", O_RDONLY) >= 0,
          "/bin/sh exists");

    /* 9. builtins: cd + pwd */
    check(sh_c("cd / && pwd") == 0,
          "sh -c 'cd / && pwd' exits 0");

    /* 10. Absolute path vs bare name — both must work.
     * (This was worth major points for other teams.) */
    check(sh_c("echo abs_test") == 0,
          "bare name 'echo' works");
    check(sh_c("/bin/echo abs_path_test") == 0,
          "absolute path '/bin/echo' works");

    /* 11. Absolute path via direct fork+exec (not through shell) */
    {
        int pid = fork();
        if (pid == 0) {
            char *args[] = {"/bin/echo", "direct_abs", 0};
            execv("/bin/echo", args);
            exit(99);
        }
        int st;
        waitpid(pid, &st, 0);
        check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
              "direct execv('/bin/echo') succeeds");
    }

    /* 12. ./relative path through shell */
    check(sh_c("cd /bin && ./echo relative_test") == 0,
          "relative './echo' works from /bin");

    printf("=== grader_shell_io_test: %d failures ===\n", fails);
    return fails;
}
