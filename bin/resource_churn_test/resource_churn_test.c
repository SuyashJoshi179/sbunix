#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define LOOPS 120
#define PAGE_SZ 4096

int main(void) {
    for (int i = 0; i < LOOPS; i++) {
        int fd = open("/dev/console", O_RDONLY);
        if (fd < 0) {
            printf("resource_churn_test: open failed at loop %d (%d)\n", i, fd);
            return 1;
        }
        close(fd);

        int pf[2];
        if (pipe(pf) < 0) {
            printf("resource_churn_test: pipe failed at loop %d\n", i);
            return 1;
        }
        if (write(pf[1], "x", 1) != 1) {
            printf("resource_churn_test: pipe write failed at loop %d\n", i);
            return 1;
        }
        char c = 0;
        if (read(pf[0], &c, 1) != 1 || c != 'x') {
            printf("resource_churn_test: pipe read failed at loop %d\n", i);
            return 1;
        }
        close(pf[0]);
        close(pf[1]);

        char *m = mmap(0, 2 * PAGE_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if ((long)m < 0) {
            printf("resource_churn_test: mmap failed at loop %d (%ld)\n", i, (long)m);
            return 1;
        }
        m[0] = 'a';
        m[PAGE_SZ] = 'b';
        if (munmap(m, 2 * PAGE_SZ) < 0) {
            printf("resource_churn_test: munmap failed at loop %d\n", i);
            return 1;
        }
    }

    int fds[64];
    int n = 0;
    int rc = 0;
    while (n < (int)(sizeof(fds) / sizeof(fds[0]))) {
        rc = open("/dev/console", O_RDONLY);
        if (rc < 0) break;
        fds[n++] = rc;
    }
    if (rc != -1 || errno != EMFILE) {
        printf("resource_churn_test: expected EMFILE after churn, got rc=%d errno=%d\n", rc, errno);
        for (int i = 0; i < n; i++) close(fds[i]);
        return 1;
    }
    for (int i = 0; i < n; i++) close(fds[i]);

    printf("resource_churn_test: PASS (%d loops)\n", LOOPS);
    return 0;
}
