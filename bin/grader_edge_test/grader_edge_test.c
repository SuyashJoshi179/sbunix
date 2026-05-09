/*
 * grader_edge_test — Subtle edge cases a professor would test.
 *
 * These aren't crashes — they're semantic correctness issues where
 * the OS gives the WRONG answer rather than crashing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>

static int fails = 0;
static void check(int cond, const char *name) {
    if (cond) printf("[grader_edge_test] PASS  %s\n", name);
    else { printf("[grader_edge_test] FAIL  %s\n", name); fails++; }
}

int main(void) {
    printf("=== grader_edge_test ===\n");

    /* 1. write returns the correct byte count, not just 0 or 1 */
    {
        int fd = open("/mnt/tmp_edge_write", O_CREAT | O_WRONLY);
        check(fd >= 0, "open for write count test");
        if (fd >= 0) {
            long w = write(fd, "abcdef", 6);
            check(w == 6, "write returns exact byte count (6)");

            /* Large write */
            char bigbuf[8192];
            memset(bigbuf, 'X', 8192);
            w = write(fd, bigbuf, 8192);
            check(w == 8192, "write(8192) returns 8192");
            close(fd);
            unlink("/mnt/tmp_edge_write");
        }
    }

    /* 2. read returns the correct byte count */
    {
        int fd = open("/mnt/tmp_edge_read", O_CREAT | O_WRONLY);
        if (fd >= 0) {
            write(fd, "hello", 5);
            close(fd);
            fd = open("/mnt/tmp_edge_read", O_RDONLY);
            if (fd >= 0) {
                char buf[64];
                long r = read(fd, buf, 64);
                check(r == 5, "read returns actual bytes (5), not buffer size");

                /* Read again — should be EOF (0) */
                r = read(fd, buf, 64);
                check(r == 0, "read at EOF returns 0");
                close(fd);
            }
            unlink("/mnt/tmp_edge_read");
        }
    }

    /* 3. stat st_size is correct */
    {
        int fd = open("/mnt/tmp_edge_stat", O_CREAT | O_WRONLY);
        if (fd >= 0) {
            write(fd, "1234567890", 10);
            struct stat st;
            fstat(fd, &st);
            check(st.st_size == 10, "fstat st_size == 10 after writing 10 bytes");
            close(fd);
            unlink("/mnt/tmp_edge_stat");
        }
    }

    /* 4. Multiple writes accumulate — not overwrite */
    {
        int fd = open("/mnt/tmp_edge_accum", O_CREAT | O_WRONLY);
        if (fd >= 0) {
            write(fd, "aaa", 3);
            write(fd, "bbb", 3);
            write(fd, "ccc", 3);
            close(fd);

            fd = open("/mnt/tmp_edge_accum", O_RDONLY);
            if (fd >= 0) {
                char buf[16] = {0};
                long r = read(fd, buf, 16);
                check(r == 9, "3 writes of 3 = 9 bytes total");
                check(memcmp(buf, "aaabbbccc", 9) == 0,
                      "writes accumulate in order");
                close(fd);
            }
            unlink("/mnt/tmp_edge_accum");
        }
    }

    /* 5. O_TRUNC clears existing file */
    {
        int fd = open("/mnt/tmp_edge_trunc", O_CREAT | O_WRONLY);
        if (fd >= 0) {
            write(fd, "old data here", 13);
            close(fd);

            fd = open("/mnt/tmp_edge_trunc", O_WRONLY | O_TRUNC);
            if (fd >= 0) {
                write(fd, "new", 3);
                close(fd);

                fd = open("/mnt/tmp_edge_trunc", O_RDONLY);
                if (fd >= 0) {
                    char buf[32] = {0};
                    long r = read(fd, buf, 32);
                    check(r == 3, "O_TRUNC: file is 3 bytes, not 13");
                    check(memcmp(buf, "new", 3) == 0, "O_TRUNC: content is 'new'");
                    close(fd);
                }
            }
            unlink("/mnt/tmp_edge_trunc");
        }
    }

    /* 6. O_APPEND writes at end */
    {
        int fd = open("/mnt/tmp_edge_append", O_CREAT | O_WRONLY);
        if (fd >= 0) {
            write(fd, "first", 5);
            close(fd);

            fd = open("/mnt/tmp_edge_append", O_WRONLY | O_APPEND);
            if (fd >= 0) {
                write(fd, "second", 6);
                close(fd);

                fd = open("/mnt/tmp_edge_append", O_RDONLY);
                if (fd >= 0) {
                    char buf[32] = {0};
                    read(fd, buf, 32);
                    check(strcmp(buf, "firstsecond") == 0,
                          "O_APPEND writes at end");
                    close(fd);
                }
            }
            unlink("/mnt/tmp_edge_append");
        }
    }

    /* 7. opendir + readdir lists expected entries */
    {
        DIR *d = opendir("/");
        check(d != 0, "opendir(/) succeeds");
        if (d) {
            int found_bin = 0, found_etc = 0;
            struct dirent *e;
            while ((e = readdir(d)) != 0) {
                if (strcmp(e->d_name, "bin") == 0) found_bin = 1;
                if (strcmp(e->d_name, "etc") == 0) found_etc = 1;
            }
            closedir(d);
            check(found_bin, "readdir(/) finds 'bin'");
            check(found_etc, "readdir(/) finds 'etc'");
        }
    }

    /* 8. getcwd(NULL, 0) — glibc extension BusyBox relies on */
    {
        char *cwd = getcwd(0, 0);
        check(cwd != 0, "getcwd(NULL, 0) allocates and returns");
        if (cwd) {
            check(cwd[0] == '/', "getcwd starts with /");
            free(cwd);
        }
    }

    /* 9. fork preserves open file descriptors */
    {
        int fd = open("/mnt/tmp_edge_forkfd", O_CREAT | O_RDWR);
        if (fd >= 0) {
            write(fd, "shared", 6);
            int pid = fork();
            if (pid == 0) {
                /* Child: write more to the same fd */
                write(fd, "_child", 6);
                close(fd);
                exit(0);
            }
            waitpid(pid, 0, 0);
            /* Parent reads from beginning */
            lseek(fd, 0, SEEK_SET);
            char buf[32] = {0};
            long r = read(fd, buf, 32);
            check(r == 12, "fork: shared fd has 12 bytes");
            check(memcmp(buf, "shared_child", 12) == 0,
                  "fork: shared fd writes accumulate");
            close(fd);
            unlink("/mnt/tmp_edge_forkfd");
        }
    }

    /* 10. Child inherits CWD */
    {
        char parent_cwd[256];
        getcwd(parent_cwd, sizeof(parent_cwd));

        int pid = fork();
        if (pid == 0) {
            char child_cwd[256];
            getcwd(child_cwd, sizeof(child_cwd));
            exit(strcmp(parent_cwd, child_cwd) == 0 ? 0 : 1);
        }
        int st;
        waitpid(pid, &st, 0);
        check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
              "child inherits parent CWD");
    }

    /* 11. Pipe large data (> 1 page) */
    {
        int pfd[2];
        if (pipe(pfd) == 0) {
            int pid = fork();
            if (pid == 0) {
                close(pfd[0]);
                char buf[8192];
                memset(buf, 'P', 8192);
                long total = 0;
                while (total < 8192) {
                    long w = write(pfd[1], buf + total, 8192 - total);
                    if (w <= 0) break;
                    total += w;
                }
                close(pfd[1]);
                exit(total == 8192 ? 0 : 1);
            }
            close(pfd[1]);
            char buf[8192];
            long total = 0;
            while (total < 8192) {
                long r = read(pfd[0], buf + total, 8192 - total);
                if (r <= 0) break;
                total += r;
            }
            close(pfd[0]);
            int st;
            waitpid(pid, &st, 0);
            check(total == 8192, "pipe: 8KB data transferred");
            int all_p = 1;
            for (int i = 0; i < 8192; i++)
                if (buf[i] != 'P') { all_p = 0; break; }
            check(all_p, "pipe: 8KB data integrity");
        }
    }

    /* 12. exit status 0 vs 255 boundary */
    {
        int pid = fork();
        if (pid == 0) exit(0);
        int st;
        waitpid(pid, &st, 0);
        check(WEXITSTATUS(st) == 0, "exit(0) => WEXITSTATUS 0");

        pid = fork();
        if (pid == 0) exit(255);
        waitpid(pid, &st, 0);
        check(WEXITSTATUS(st) == 255, "exit(255) => WEXITSTATUS 255");

        pid = fork();
        if (pid == 0) exit(256);
        waitpid(pid, &st, 0);
        check(WEXITSTATUS(st) == 0, "exit(256) wraps to WEXITSTATUS 0");
    }

    printf("=== grader_edge_test: %d failures ===\n", fails);
    return fails;
}
