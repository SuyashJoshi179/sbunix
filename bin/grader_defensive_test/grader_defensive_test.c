/*
 * grader_defensive_test — Kernel defensiveness.
 *
 * The kernel must distinguish "nothing to do" from "request was
 * nonsensical". Bad handles must fail, never silently succeed.
 * (Based on Eval 05, worth 57 points.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[grader_defensive_test] PASS  %s\n", name);
    } else {
        printf("[grader_defensive_test] FAIL  %s\n", name);
        fails++;
    }
}

int main(void) {
    printf("=== grader_defensive_test ===\n");

    /* 1. read on never-opened fd */
    char buf[64];
    long r = read(42, buf, sizeof(buf));
    check(r < 0, "read(never-opened fd=42) fails");

    /* 2. Open then close, then try to use the closed fd */
    int fd = open("/bin/echo", O_RDONLY);
    if (fd >= 0) {
        close(fd);
        r = read(fd, buf, sizeof(buf));
        check(r < 0, "read(closed fd) fails");

        long w = write(fd, "x", 1);
        check(w < 0, "write(closed fd) fails");

        int rc = close(fd);
        check(rc < 0, "close(already-closed fd) fails");
    } else {
        /* Skip these if /bin/echo doesn't exist */
        check(1, "skipped closed-fd tests (no /bin/echo)");
    }

    /* 3. fstat on invalid fd */
    struct stat st;
    int rc = fstat(-1, &st);
    check(rc < 0, "fstat(-1) fails");

    rc = fstat(999, &st);
    check(rc < 0, "fstat(999) fails");

    /* 4. lseek on bad fd */
    long off = lseek(999, 0, 0);
    check(off < 0, "lseek(fd=999) fails");

    /* 5. dup2 with bad newfd */
    fd = open("/bin/echo", O_RDONLY);
    if (fd >= 0) {
        rc = dup2(fd, -1);
        check(rc < 0, "dup2(valid, -1) fails");
        close(fd);
    }

    /* 6. exec non-existent binary */
    {
        int pid = fork();
        if (pid == 0) {
            char *args[] = {"/nonexistent_binary_xyz", 0};
            int e = execv("/nonexistent_binary_xyz", args);
            /* execv should return -1 on failure; if we get here, it failed correctly */
            exit(e < 0 ? 0 : 1);
        }
        int st2;
        waitpid(pid, &st2, 0);
        check(WIFEXITED(st2) && WEXITSTATUS(st2) == 0,
              "execv(nonexistent) returns error");
    }

    /* 7. mkdir on existing directory */
    rc = mkdir("/", 0755);
    check(rc < 0, "mkdir('/') fails (already exists)");

    /* 8. Distinguish "nothing to do" from "nonsensical":
     *    read(valid_fd, buf, 0) should return 0 (success, zero bytes)
     *    read(bad_fd, buf, 10) should return -1 (error)  */
    fd = open("/bin/echo", O_RDONLY);
    if (fd >= 0) {
        r = read(fd, buf, 0);
        check(r == 0, "read(valid_fd, 0 bytes) returns 0 (not error)");

        long r2 = read(999, buf, 10);
        check(r2 < 0, "read(bad_fd, 10 bytes) returns error");
        close(fd);
    }

    /* 9. write to read-only fd */
    fd = open("/bin/echo", O_RDONLY);
    if (fd >= 0) {
        long w = write(fd, "test", 4);
        check(w < 0, "write(read-only fd) fails");
        close(fd);
    }

    /* 10. pipe with null pointer */
    rc = pipe(0);
    check(rc < 0, "pipe(NULL) fails");

    printf("=== grader_defensive_test: %d failures ===\n", fails);
    return fails;
}
