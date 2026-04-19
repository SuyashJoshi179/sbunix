#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE_SZ 4096

static int child_exec_echo(void) {
    int pid = fork();
    if (pid < 0) return pid;
    if (pid == 0) {
        char *argv[] = {"echo", "meminfo_regression", 0};
        execv("/bin/echo", argv);
        exit(127);
    }

    int st = -1;
    while (1) {
        int got = wait(&st);
        if (got == pid) break;
        if (got == -EINTR) continue;
        if (got < 0) return got;
    }
    return st == 0 ? 0 : -1;
}

int main(void) {
    long before = meminfo();
    if (before <= 0) {
        printf("meminfo_regression_test: invalid meminfo before %ld\n", before);
        return 1;
    }

    for (int i = 0; i < 80; i++) {
        int fd = open("/dev/console", O_RDONLY);
        if (fd < 0) {
            printf("meminfo_regression_test: open failed at %d (%d)\n", i, fd);
            return 1;
        }
        close(fd);

        char *m = mmap(0, 2 * PAGE_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if ((long)m < 0) {
            printf("meminfo_regression_test: mmap failed at %d (%ld)\n", i, (long)m);
            return 1;
        }
        m[0] = 'm';
        m[PAGE_SZ] = 'n';
        if (munmap(m, 2 * PAGE_SZ) < 0) {
            printf("meminfo_regression_test: munmap failed at %d\n", i);
            return 1;
        }

        if ((i % 8) == 0) {
            int r = child_exec_echo();
            if (r < 0) {
                printf("meminfo_regression_test: fork/exec/wait failed at %d (%d)\n", i, r);
                return 1;
            }
        }
    }

    long after = meminfo();
    if (after != before) {
        printf("meminfo_regression_test: leak detected before=%ld after=%ld\n", before, after);
        return 1;
    }

    printf("meminfo_regression_test: PASS (before=%ld after=%ld)\n", before, after);
    return 0;
}
