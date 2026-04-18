#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[munmap_test] PASS  %s\n", name);
    } else {
        printf("[munmap_test] FAIL  %s\n", name);
        fails++;
    }
}

int main(void) {
    printf("=== munmap_test ===\n");

    void *p = mmap(0, 3 * 4096, PROT_READ | PROT_WRITE,
                   MAP_ANON | MAP_PRIVATE, -1, 0);
    check((long)p > 0, "mmap 3 pages");

    volatile char *buf = (volatile char *)p;
    buf[0] = 'A';
    buf[4096] = 'B';
    buf[2 * 4096] = 'C';
    check(buf[0] == 'A' && buf[4096] == 'B' && buf[2 * 4096] == 'C',
          "write all 3 pages");

    int rc = munmap((char *)p + 4096, 4096);
    check(rc == 0, "munmap middle page returns 0");

    check(buf[0] == 'A', "first page still readable after middle munmap");
    check(buf[2 * 4096] == 'C', "third page still readable after middle munmap");

    int pid = fork();
    if (pid == 0) {
        volatile char *mp = (volatile char *)p + 4096;
        *mp = 'X';
        exit(99);
    }
    int st;
    wait(&st);
    check(st != 0, "child touching unmapped middle page killed");

    rc = munmap(p, 4096);
    check(rc == 0, "munmap first page");
    rc = munmap((char *)p + 2 * 4096, 4096);
    check(rc == 0, "munmap third page");

    int rc2 = munmap(p, 4096);
    check(rc2 != 0, "double munmap returns error");

    void *q = mmap(0, 4096, PROT_READ | PROT_WRITE,
                   MAP_ANON | MAP_PRIVATE, -1, 0);
    check((long)q > 0, "mmap after munmap succeeds");
    munmap(q, 4096);

    printf("=== munmap_test: %d failures ===\n", fails);
    return fails;
}
