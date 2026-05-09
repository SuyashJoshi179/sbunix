/*
 * grader_fileio_test — File I/O, stat, dup, directory ops, buffered stdio.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

static int fails = 0;
static void check(int cond, const char *name) {
    if (cond) printf("[grader_fileio_test] PASS  %s\n", name);
    else { printf("[grader_fileio_test] FAIL  %s\n", name); fails++; }
}

int main(void) {
    printf("=== grader_fileio_test ===\n");

    /* 1. write + read round-trip */
    int fd = open("/mnt/tmp_grader_test", O_CREAT | O_WRONLY);
    check(fd >= 0, "open(O_CREAT|O_WRONLY) succeeds");
    if (fd >= 0) {
        long w = write(fd, "hello", 5);
        check(w == 5, "write 5 bytes");
        close(fd);

        fd = open("/mnt/tmp_grader_test", O_RDONLY);
        check(fd >= 0, "reopen O_RDONLY succeeds");
        if (fd >= 0) {
            char buf[16] = {0};
            long r = read(fd, buf, sizeof(buf));
            check(r == 5, "read returns 5 bytes");
            check(memcmp(buf, "hello", 5) == 0, "read data matches");
            close(fd);
        }
        unlink("/mnt/tmp_grader_test");
    }

    /* 2. lseek */
    fd = open("/mnt/tmp_grader_seek", O_CREAT | O_RDWR);
    if (fd >= 0) {
        write(fd, "abcdefgh", 8);
        long pos = lseek(fd, 3, SEEK_SET);
        check(pos == 3, "lseek(SEEK_SET, 3) returns 3");
        char c = 0;
        read(fd, &c, 1);
        check(c == 'd', "read after seek returns 'd'");

        pos = lseek(fd, 0, SEEK_END);
        check(pos == 8, "lseek(SEEK_END) returns file size 8");
        close(fd);
        unlink("/mnt/tmp_grader_seek");
    }

    /* 3. fstat */
    fd = open("/mnt/tmp_grader_stat", O_CREAT | O_WRONLY);
    if (fd >= 0) {
        write(fd, "test", 4);
        struct stat st;
        int rc = fstat(fd, &st);
        check(rc == 0, "fstat succeeds");
        check(st.st_size == 4, "fstat st_size == 4");
        close(fd);
        unlink("/mnt/tmp_grader_stat");
    }

    /* 4. stat("/") is a directory */
    {
        struct stat st;
        int rc = fstat(open("/", O_RDONLY), &st);
        check(rc == 0, "stat(/) succeeds");
        check(S_ISDIR(st.st_mode), "/ is a directory");
    }

    /* 5. dup */
    fd = open("/mnt/tmp_grader_dup", O_CREAT | O_RDWR);
    if (fd >= 0) {
        int fd2 = dup(fd);
        check(fd2 >= 0 && fd2 != fd, "dup returns new fd");
        write(fd, "dup", 3);
        lseek(fd2, 0, SEEK_SET);
        char buf[8] = {0};
        long r = read(fd2, buf, 8);
        check(r == 3 && memcmp(buf, "dup", 3) == 0,
              "dup'd fd reads same data");
        close(fd2);
        close(fd);
        unlink("/mnt/tmp_grader_dup");
    }

    /* 6. dup2 */
    fd = open("/mnt/tmp_grader_dup2", O_CREAT | O_RDWR);
    if (fd >= 0) {
        int rc = dup2(fd, 10);
        check(rc == 10, "dup2(fd, 10) returns 10");
        write(10, "d2", 2);
        lseek(fd, 0, SEEK_SET);
        char buf[8] = {0};
        read(fd, buf, 8);
        check(memcmp(buf, "d2", 2) == 0, "dup2 fd shares file position");
        close(10);
        close(fd);
        unlink("/mnt/tmp_grader_dup2");
    }

    /* 7. unlink makes file disappear */
    fd = open("/mnt/tmp_grader_unlink", O_CREAT | O_WRONLY);
    if (fd >= 0) {
        close(fd);
        int rc = unlink("/mnt/tmp_grader_unlink");
        check(rc == 0, "unlink succeeds");
        errno = 0;
        fd = open("/mnt/tmp_grader_unlink", O_RDONLY);
        check(fd < 0, "open after unlink fails");
        if (fd >= 0) close(fd);
    }

    /* 8. mkdir + chdir + getcwd */
    {
        int rc = mkdir("/mnt/tmp_grader_dir", 0755);
        check(rc == 0, "mkdir succeeds");
        if (rc == 0) {
            rc = chdir("/mnt/tmp_grader_dir");
            check(rc == 0, "chdir to new dir succeeds");
            char cwdbuf[256];
            char *cwd = getcwd(cwdbuf, sizeof(cwdbuf));
            check(cwd != 0, "getcwd succeeds");
            if (cwd) {
                check(strcmp(cwd, "/mnt/tmp_grader_dir") == 0,
                      "getcwd returns correct path");
            }
            chdir("/");
        }
    }

    /* 9. Buffered stdio round-trip */
    {
        FILE *fp = fopen("/mnt/tmp_grader_stdio", "w");
        check(fp != 0, "fopen(w) succeeds");
        if (fp) {
            fprintf(fp, "line1\n");
            fclose(fp);
            fp = fopen("/mnt/tmp_grader_stdio", "r");
            check(fp != 0, "fopen(r) succeeds");
            if (fp) {
                char buf[32];
                char *got = fgets(buf, sizeof(buf), fp);
                check(got != 0, "fgets returns non-NULL");
                check(strcmp(buf, "line1\n") == 0, "fgets content matches");
                fclose(fp);
            }
            unlink("/mnt/tmp_grader_stdio");
        }
    }

    /* 10. fileno */
    check(fileno(stdout) == 1, "fileno(stdout) == 1");
    check(fileno(stderr) == 2, "fileno(stderr) == 2");

    printf("=== grader_fileio_test: %d failures ===\n", fails);
    return fails;
}
