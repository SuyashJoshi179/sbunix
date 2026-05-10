#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

/* Adapted: VMA cap and page cap have been removed. Only NOFILE is still
 * enforced. We additionally verify many mmaps succeed (covered in detail
 * by many_mmaps_test). */

#define PAGE_SZ 4096

static int pass, fail;

static void check(int cond, const char *name) {
    if (cond) { printf("PASS  %s\n", name); pass++; }
    else      { printf("FAIL  %s\n", name); fail++; }
}

int main(void) {
    int fds[64];
    int nfds = 0;
    int r = 0;

    while (nfds < (int)(sizeof(fds) / sizeof(fds[0]))) {
        r = open("/dev/console", O_RDONLY);
        if (r < 0) break;
        fds[nfds++] = r;
    }
    check(r == -1 && errno == EMFILE, "NOFILE cap enforced with -EMFILE");

    int dup2_rc = dup2(fds[0], 9999);
    check(dup2_rc == -1 && errno == EBADF, "dup2 newfd beyond limit returns -EBADF");

    for (int i = 0; i < nfds; i++) close(fds[i]);

    /* Many mmaps now succeed (slab pool). */
    void *maps[64];
    int nmaps = 0;
    for (; nmaps < 64; nmaps++) {
        void *p = mmap(0, PAGE_SZ, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == (void *)-1) break;
        maps[nmaps] = p;
    }
    check(nmaps == 64, "64 anon mmaps succeed (slab pool)");
    for (int i = 0; i < nmaps; i++) munmap(maps[i], PAGE_SZ);

    /* sbrk still works for legacy callers (page cap removed). */
    void *brk0 = sbrk(0);
    void *brk1 = sbrk(PAGE_SZ);
    check((long)brk1 != -1 && (char *)sbrk(0) == (char *)brk0 + PAGE_SZ,
          "sbrk grows without page cap");
    sbrk(-PAGE_SZ);

    printf("rlimit_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
