#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE_SZ 4096UL
#define KERNEL_BASE_VA 0xFFFFFFFF00000000UL

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
    char *r = mmap(0, 3 * PAGE_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if ((long)r < 0) {
        printf("copyio_fuzz_test: mmap failed %ld\n", (long)r);
        return 1;
    }

    for (int i = 0; i < (int)(3 * PAGE_SZ); i++)
        r[i] = (char)('a' + (i % 26));

    check(munmap(r + PAGE_SZ, PAGE_SZ) == 0, "create middle unmapped hole");

    check(write(1, r + 100, 64) == 64, "write from valid mapped region");
    check(write(1, r + PAGE_SZ - 8, 16) == -EFAULT,
          "write crossing mapped->unmapped boundary fails");
        check(write(1, r + 2 * PAGE_SZ + 100, 16) == 16,
          "write crossing mapped->mapped boundary succeeds");

    int p[2];
    check(pipe(p) == 0, "pipe setup");
    check(write(p[1], "0123456789abcdef", 16) == 16, "pipe seeded");

    check(read(p[0], r + PAGE_SZ - 4, 12) == -EFAULT,
          "read crossing into unmapped page fails");
        check(write(p[1], "ABCDEFGH", 8) == 8,
            "pipe reseed after EFAULT read");
    check(read(p[0], r + 2 * PAGE_SZ + 12, 8) == 8,
          "read into valid high mapped page succeeds");

        check(getcwd(r + PAGE_SZ - 1, 32) == -EFAULT,
          "getcwd straddle into unmapped page fails");

    check(open((const char *)(uintptr_t)KERNEL_BASE_VA, 0) == -EFAULT,
          "kernel VA path rejected");

    close(p[0]);
    close(p[1]);

    printf("copyio_fuzz_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
