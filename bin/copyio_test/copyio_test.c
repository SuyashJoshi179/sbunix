#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE_SIZE_LOCAL 4096UL
#define KERNEL_BASE_VA  0xFFFFFFFF00000000UL

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
    int p[2];
    char okbuf[32];

    check(write(1, "copyio_test: begin\n", 19) > 0, "valid write pointer");

    check(write(1, (const void *)0, 1) == -EFAULT, "NULL pointer rejected");
    check(write(1, (const void *)(uintptr_t)KERNEL_BASE_VA, 1) == -EFAULT,
          "kernel-range pointer rejected");

    char *region = mmap(0, 2 * PAGE_SIZE_LOCAL,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON,
                        -1, 0);
    if ((long)region < 0) {
        printf("copyio_test: mmap failed %ld\n", (long)region);
        return 1;
    }

    for (int i = 0; i < 16; i++)
        region[i] = 'A' + i;

    check(munmap(region + PAGE_SIZE_LOCAL, PAGE_SIZE_LOCAL) == 0,
          "prepare unmapped second page");

    char *unmapped = region + PAGE_SIZE_LOCAL;
    char *straddle = region + PAGE_SIZE_LOCAL - 8;

    check(open(unmapped, 0) == -EFAULT, "unmapped path pointer rejected");
    check(write(1, straddle, 16) == -EFAULT, "straddling write pointer rejected");

    check(pipe(p) == 0, "pipe setup succeeds");
    check(write(p[1], "0123456789", 10) == 10, "pipe write valid buffer");
    check(read(p[0], okbuf, 10) == 10, "pipe read valid pointer");

    check(write(p[1], "abcdefghijklmnop", 16) == 16, "pipe write for straddle read");
    check(read(p[0], straddle, 16) == -EFAULT, "straddling read pointer rejected");

    check(pipe((int *)(region + PAGE_SIZE_LOCAL - 4)) == -EFAULT,
          "straddling pipe fds rejected");
    check(getcwd(region + PAGE_SIZE_LOCAL - 1, 16) == -EFAULT,
          "straddling getcwd pointer rejected");

    close(p[0]);
    close(p[1]);

    printf("copyio_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
