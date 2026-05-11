/*
 * cow_pcache_refleak_test — regression for audit T1.13
 *
 * Pre-fix lifecycle for MAP_PRIVATE file mappings forked across procs:
 *
 *   parent fault    → install pcache PTE, NO page_get (refs[pa]=1)
 *   uvmcow_share    → page_get(pa) for child (refs[pa]=2)
 *   *neither* exit  → page_put on pcache pa, because vma_drop_file_pages
 *                     clears the PTE first
 *
 * Net: one ref leaked per fork. Eventually page_get panicked with
 * "refcount overflow" at 65535.
 *
 * Plus, after the parent did its first CoW (PTE now → anon), forking
 * again and the child writing took the same file-CoW branch with old_pa
 * = anon. The branch unconditionally ran pcache_get(...); pcache_put×2;
 * which (a) decremented an unrelated slot's refcnt by 2 and (b) leaked
 * page_refs[anon_pfn] by 1 because page_put(old_pa) was missing.
 *
 * This test exercises both paths. It can't directly read kernel refs[],
 * so it relies on volume: many iterations until either a panic fires
 * (hang/crash on broken build) or completes cleanly on a fixed build.
 *
 * Iteration count chosen above the LRU pressure threshold but well
 * below 65535 — we just need enough cycles that the pre-fix leak/
 * underflow has multiple chances to misbehave before any single test
 * timeout hits.
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <string.h>

#define ITERS 50

static int fails = 0;
static void check(int cond, const char *name) {
    if (cond) {
        printf("[cow_pcache_refleak] PASS  %s\n", name);
    } else {
        printf("[cow_pcache_refleak] FAIL  %s\n", name);
        fails++;
    }
}

int main(void) {
    printf("=== cow_pcache_refleak_test ===\n");

    /* Path A: fork without writing in parent.
     * Parent faults in pcache page, then forks, child writes (file-CoW
     * branch with old_pa = pcache), child exits. Pre-fix: leaks one
     * page_refs[pcache_pfn] per iteration. */
    int loopA_ok = 1;
    for (int i = 0; i < ITERS; i++) {
        int fd = open("/etc/rc", O_RDONLY);
        if (fd < 0) { loopA_ok = 0; break; }
        char *p = mmap(0, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE, fd, 0);
        if (p == (void *)-1) { close(fd); loopA_ok = 0; break; }

        /* Parent reads to ensure pcache PTE is installed. */
        volatile char unused = p[0]; (void)unused;

        int pid = fork();
        if (pid == 0) {
            /* CoW write: file-CoW branch, old_pa = pcache. */
            p[0] = 'X';
            _exit(0);
        }
        if (pid < 0) { munmap(p, 4096); close(fd); loopA_ok = 0; break; }
        int st = 0;
        wait(&st);
        munmap(p, 4096);
        close(fd);
    }
    check(loopA_ok, "50 iterations of map+fork+child-CoW+exit");

    /* Path B: parent does its own CoW *before* forking. Subsequent
     * write in the child takes the file-CoW branch with old_pa = anon
     * (pre-fix: corrupts an unrelated pcache slot's refcnt). */
    int loopB_ok = 1;
    for (int i = 0; i < ITERS; i++) {
        int fd = open("/etc/rc", O_RDONLY);
        if (fd < 0) { loopB_ok = 0; break; }
        char *p = mmap(0, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE, fd, 0);
        if (p == (void *)-1) { close(fd); loopB_ok = 0; break; }

        /* Parent CoW first: PTE → anon. */
        p[0] = 'A';

        int pid = fork();
        if (pid == 0) {
            /* Child write: file-CoW branch, old_pa = anon. */
            p[0] = 'B';
            _exit(0);
        }
        if (pid < 0) { munmap(p, 4096); close(fd); loopB_ok = 0; break; }
        int st = 0;
        wait(&st);
        munmap(p, 4096);
        close(fd);
    }
    check(loopB_ok, "50 iterations of map+parent-CoW+fork+child-CoW+exit");

    /* Path C: nested fork (no parent write). Confirms multi-level
     * uvmcow_share doesn't compound the leak. */
    int loopC_ok = 1;
    for (int i = 0; i < ITERS; i++) {
        int fd = open("/etc/rc", O_RDONLY);
        if (fd < 0) { loopC_ok = 0; break; }
        char *p = mmap(0, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE, fd, 0);
        if (p == (void *)-1) { close(fd); loopC_ok = 0; break; }

        volatile char unused = p[0]; (void)unused;

        int pid = fork();
        if (pid == 0) {
            int gpid = fork();
            if (gpid == 0) {
                p[0] = 'Y';
                _exit(0);
            }
            int st2 = 0;
            wait(&st2);
            _exit(0);
        }
        if (pid < 0) { munmap(p, 4096); close(fd); loopC_ok = 0; break; }
        int st = 0;
        wait(&st);
        munmap(p, 4096);
        close(fd);
    }
    check(loopC_ok, "50 iterations of map+grandchild-CoW+exit");

    if (fails) {
        printf("cow_pcache_refleak_test: %d failed\n", fails);
        return 1;
    }
    printf("cow_pcache_refleak_test: all passed\n");
    return 0;
}
