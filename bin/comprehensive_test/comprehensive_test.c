#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[comprehensive_test] PASS  %s\n", name);
    } else {
        printf("[comprehensive_test] FAIL  %s\n", name);
        fails++;
    }
}

static void test_malloc_stress(void) {
    void *ptrs[100];
    int alloc_ok = 1;
    for (int i = 0; i < 100; i++) {
        ptrs[i] = malloc(16 + i * 8);
        if (!ptrs[i]) { alloc_ok = 0; break; }
        memset(ptrs[i], (char)(i & 0xFF), 16 + i * 8);
    }
    check(alloc_ok, "malloc 100 varying blocks");

    int verify_ok = 1;
    for (int i = 0; i < 100; i++) {
        if (!ptrs[i]) continue;
        unsigned char *p = (unsigned char *)ptrs[i];
        for (int j = 0; j < 16 + i * 8; j++) {
            if (p[j] != (unsigned char)(i & 0xFF)) { verify_ok = 0; break; }
        }
        if (!verify_ok) break;
    }
    check(verify_ok, "malloc: data integrity across 100 blocks");

    for (int i = 0; i < 100; i += 2)
        free(ptrs[i]);
    for (int i = 1; i < 100; i += 2)
        free(ptrs[i]);

    void *reuse[50];
    int reuse_ok = 1;
    for (int i = 0; i < 50; i++) {
        reuse[i] = malloc(32);
        if (!reuse[i]) { reuse_ok = 0; break; }
    }
    check(reuse_ok, "malloc: re-allocate after free-all");
    for (int i = 0; i < 50; i++)
        free(reuse[i]);
}

static void test_fork_exec_combo(void) {
    int pid = fork();
    if (pid == 0) {
        void *p = malloc(256);
        check(p != 0, "child: malloc after fork");
        memset(p, 'X', 256);

        void *m = mmap(0, 4096, PROT_READ | PROT_WRITE,
                       MAP_ANON | MAP_PRIVATE, -1, 0);
        check((long)m > 0, "child: mmap after fork");
        volatile char *mp = (volatile char *)m;
        mp[0] = 'M';
        check(mp[0] == 'M', "child: mmap region writable");

        free(p);
        munmap(m, 4096);
        exit(0);
    }
    int st;
    wait(&st);
    check(st == 0, "fork+malloc+mmap child succeeded");
}

static void test_cow_then_mmap(void) {
    volatile int shared = 42;

    int pid = fork();
    if (pid == 0) {
        shared = 99;
        void *m = mmap(0, 4096, PROT_READ | PROT_WRITE,
                       MAP_ANON | MAP_PRIVATE, -1, 0);
        check((long)m > 0, "cow child: mmap succeeds");
        volatile char *p = (volatile char *)m;
        p[0] = 'Z';
        check(p[0] == 'Z', "cow child: mmap write after COW fault");
        munmap(m, 4096);
        exit(0);
    }
    int st;
    wait(&st);
    check(st == 0, "cow+mmap child succeeded");
    check(shared == 42, "parent: shared unchanged");
}

static void test_sbrk_mmap_coexist(void) {
    void *brk0 = sbrk(0);
    sbrk(4096);
    volatile char *hp = (volatile char *)brk0;
    hp[0] = 'H';

    void *m = mmap(0, 4096, PROT_READ | PROT_WRITE,
                   MAP_ANON | MAP_PRIVATE, -1, 0);
    check((long)m > 0, "mmap while heap active");
    volatile char *mp = (volatile char *)m;
    mp[0] = 'M';

    check(hp[0] == 'H', "heap intact after mmap");
    check(mp[0] == 'M', "mmap intact with active heap");

    munmap(m, 4096);
    sbrk(-4096);
}

static void test_deep_fork_chain(void) {
    int depth = 0;
    int is_leaf = 0;

    for (int i = 0; i < 4; i++) {
        int pid = fork();
        if (pid == 0) {
            depth = i + 1;
            if (i == 3) is_leaf = 1;
            continue;
        }
        int st;
        wait(&st);
        if (depth == 0) {
            check(st == 0, "fork chain: all levels completed");
        }
        if (is_leaf) exit(0);
        exit(st);
    }
    if (is_leaf) {
        check(depth == 4, "leaf: reached depth 4");
        exit(0);
    }
}

int main(void) {
    printf("=== comprehensive_test ===\n");

    test_malloc_stress();
    test_fork_exec_combo();
    test_cow_then_mmap();
    test_sbrk_mmap_coexist();
    test_deep_fork_chain();

    printf("=== comprehensive_test: %d failures ===\n", fails);
    return fails;
}
