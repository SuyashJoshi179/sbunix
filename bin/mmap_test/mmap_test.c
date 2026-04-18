#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[mmap_test] PASS  %s\n", name);
    } else {
        printf("[mmap_test] FAIL  %s\n", name);
        fails++;
    }
}

int main(void) {
    printf("=== mmap_test ===\n");

    void *p = mmap(0, 8192, PROT_READ | PROT_WRITE,
                   MAP_ANON | MAP_PRIVATE, -1, 0);
    check((long)p > 0, "mmap returns valid address");
    check(((long)p & 0xFFF) == 0, "mmap address is page-aligned");

    volatile char *buf = (volatile char *)p;
    buf[0] = 'H';
    buf[4095] = 'e';
    buf[4096] = 'l';
    buf[8191] = 'o';
    check(buf[0] == 'H' && buf[4095] == 'e', "write+read first page");
    check(buf[4096] == 'l' && buf[8191] == 'o', "write+read second page");

    int rc = munmap(p, 8192);
    check(rc == 0, "munmap returns 0");

    void *a = mmap(0, 4096, PROT_READ | PROT_WRITE,
                   MAP_ANON | MAP_PRIVATE, -1, 0);
    void *b = mmap(0, 4096, PROT_READ | PROT_WRITE,
                   MAP_ANON | MAP_PRIVATE, -1, 0);
    check(a != b, "two mmaps return different addresses");
    munmap(a, 4096);
    munmap(b, 4096);

    printf("=== mmap_test: %d failures ===\n", fails);
    return fails;
}
