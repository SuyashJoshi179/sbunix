/*
 * POSIX conformance test: <sys/mman.h>
 *
 * Reference: docs/susv5-html/basedefs/sys_mman.h.html
 *
 * Audited POSIX functions: mmap, munmap, msync
 * Required macros: PROT_*, MAP_*, MS_*, MAP_FAILED
 *
 * Excluded (POSIX functions not implemented):
 *   mprotect, mlock, munlock, mlockall, munlockall, posix_madvise,
 *   shm_open, shm_unlink, posix_typed_mem_*
 *
 * DIVERGENCE: <sys/mman.h> in our libc fails to expose size_t and off_t
 * (no include of <sys/types.h>). All function pins that use size_t/off_t
 * are commented out below. Uncomment after Phase 2 adds the include.
 *
 * Additional DIVERGENCE: our mmap/munmap/msync use `long` for size/offset
 * where POSIX requires size_t/off_t.
 */
#include <sys/mman.h>

#define PIN __attribute__((unused)) static

PIN void *(*_pin_mmap)(void *, size_t, int, int, int, off_t) = mmap;
PIN int   (*_pin_munmap)(void *, size_t) = munmap;
PIN int   (*_pin_msync)(void *, size_t, int) = msync;

__attribute__((unused))
static void _macro_checks(void) {
    int x = PROT_NONE | PROT_READ | PROT_WRITE | PROT_EXEC;
    x |= MAP_SHARED | MAP_PRIVATE | MAP_FIXED;
    x |= MS_SYNC;
    (void)x;
    (void)MAP_FAILED;
}
