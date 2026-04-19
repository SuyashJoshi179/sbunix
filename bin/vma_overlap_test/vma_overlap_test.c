#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE_SZ 4096

static int pass;
static int fail;

static void check(int cond, const char *name) {
    if (cond) {
        printf("PASS  %s\n", name);
        pass++;
    } else {
        printf("FAIL  %s\n", name);
        fail++;
    }
}

int main(void) {
    char *m = mmap(0, 4 * PAGE_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    check((long)m > 0, "mmap 4 pages");
    if ((long)m <= 0) {
        printf("vma_overlap_test: %d passed, %d failed\n", pass, fail + 1);
        return 1;
    }

    m[0] = 'a';
    m[PAGE_SZ] = 'b';
    m[2 * PAGE_SZ] = 'c';
    m[3 * PAGE_SZ] = 'd';
    check(1, "touch all pages");

    check(munmap(m + PAGE_SZ, 2 * PAGE_SZ) == 0, "munmap middle two pages");

    check(munmap(m + PAGE_SZ, PAGE_SZ) == -EINVAL, "double munmap middle returns -EINVAL");
    check(munmap(m + 1, PAGE_SZ) == -EINVAL, "unaligned munmap returns -EINVAL");

    char *m2 = mmap(0, 2 * PAGE_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    check((long)m2 > 0, "mmap after middle hole split");
    if ((long)m2 > 0) {
        m2[0] = 'x';
        m2[PAGE_SZ] = 'y';
        check(munmap(m2, 2 * PAGE_SZ) == 0, "munmap new mapping");
    }

    check(m[0] == 'a', "left fragment intact");
    check(m[3 * PAGE_SZ] == 'd', "right fragment intact");

    check(munmap(m, PAGE_SZ) == 0, "munmap left fragment");
    check(munmap(m + 3 * PAGE_SZ, PAGE_SZ) == 0, "munmap right fragment");

    printf("vma_overlap_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
