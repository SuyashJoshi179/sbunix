# Large Userspace Allocations — Design

**Status:** Approved (brainstorm phase). Ready for implementation plan.
**Date:** 2026-05-07
**Author:** kailash-anand

## Goal

Make SBUnix support very large user allocations (multi-GB to tens of GB), with
lazy physical-page allocation. malloc must use mmap (not sbrk). free must work
correctly. Resources must return to the OS on process exit. Remove unnecessary
artificial caps. Preserve all existing functionality.

## Non-goals

- Multi-arena (per-thread) malloc — single huge arena suffices.
- madvise(MADV_DONTNEED) for in-arena physical reclaim — future TODO.
- Multi-CPU/threading concerns.
- Demand-paged ELF loading — keep eager loader.

## Current state (baseline)

User VA layout (sv39 user space available: 256 GB; current usage: ~1 GB):

```
0x0000_0000_0000_1000  USER_TEXT_BASE
                       (text/data, eager-mapped by load_user_elf)
HEAP grows up via sbrk, capped at HEAP_MAX = 0x2000_0000  (512 MB ceiling)
0x0000_0000_2000_0000  MMAP_START
                       mmap() top-down search up to MMAP_END
0x0000_0000_3FF0_0000  MMAP_END (just below stack)
0x0000_0000_4000_0000  USER_STACK_TOP, max 256 pages (1 MB)
```

Caps that constrain large allocations:

- `VMA_POOL_SIZE = 256` — single global static array shared by all processes.
- `RLIM_NPAGES_DEFAULT = 256` (1 MB virtual mapped pages) — per-process; enforced
  by `sys_sbrk` and `sys_mmap` against the sum of all VMA address ranges.
- `RLIM_NVMA_DEFAULT = 64` — per-process VMA-node count cap.
- `MAX_STACK_PAGES = 256` (1 MB) — stack growth limit.
- libc `MAX_ALLOC = 128 KB` — rejects any malloc over 128 KB.
- libc malloc uses `sbrk` to grow the heap.

Lazy fault for anon VMAs already works in `kernel/vma.c::user_page_fault`.

## New address-space layout

```
0x0000_0000_0000_1000  USER_TEXT_BASE
0x0000_0001_0000_0000  HEAP_ARENA_BASE   (4 GB)
0x0000_0010_0000_0000  HEAP_ARENA_END = MMAP_BASE   (64 GB)
0x0000_0020_0000_0000  MMAP_END         (128 GB)
0x0000_0040_0000_0000  USER_STACK_TOP   (256 GB)
                        stack grows down, gated by RLIMIT_STACK
```

Region segregation: `HEAP_ARENA` is reserved exclusively for libc malloc's
arena. `MMAP` is reserved exclusively for explicit user `mmap()` calls. Kernel's
`sys_mmap` top-down search remains scoped to `MMAP_BASE..MMAP_END`. libc places
its arena in `HEAP_ARENA` via `MAP_FIXED`.

Constants live in `kernel/include/vmem.h` and `kernel/include/vma.h`.
`HEAP_MAX`, current `MMAP_START`, `MMAP_END`, `MAX_STACK_PAGES` removed or
redefined.

## Component changes

### Kernel: VMA storage (slab allocator)

Replace 256-entry static `vma_pool` with a kernel-mem-backed slab.

- `vma_slab_alloc()` / `vma_slab_free(struct vma *)`.
- Slab allocates one 4 KB page via `page_alloc()` per refill; carves into
  `(PAGE_SIZE / sizeof(struct vma))` slots (~85). Slots threaded onto a
  free-list (`v->next`).
- Slab pages never freed (slots reused indefinitely; OK for single-machine,
  long-running OS). Future TODO: reclaim empty pages.
- `vma_alloc()` / `vma_free()` continue as the public API; both delegate to
  the slab.
- Drop `RLIM_NVMA_DEFAULT` and `rlim_nvma` field. Drop `rlim_nvma` checks.

### Kernel: remove the page cap

- Drop `rlim_npages` from PCB.
- Drop `proc_vma_total_pages()` enforcement in `sys_sbrk` and `sys_mmap`.
- Real backstop becomes `page_alloc()` returning NULL inside `user_page_fault`.
  That path already returns -1, which produces SIGSEGV. The fault handler
  must propagate this cleanly so user code receives SIGSEGV rather than
  kernel panic.

### Kernel: rlimit (POSIX)

Add a small POSIX-compatible rlimit subsystem.

```c
typedef unsigned long rlim_t;
struct rlimit { rlim_t rlim_cur; rlim_t rlim_max; };
#define RLIM_INFINITY  ((rlim_t)-1)
#define RLIMIT_DATA    2
#define RLIMIT_STACK   3
#define RLIMIT_NOFILE  7
#define RLIMIT_AS      9
#define RLIMITS_NR     10  /* sparse table indexed by resource id */
```

Per-PCB array `struct rlimit rlim[RLIMITS_NR]`.

Defaults at `alloc_proc`:

| Resource     | rlim_cur                   | rlim_max     |
|--------------|----------------------------|--------------|
| RLIMIT_STACK | 8 MB (2048 pages)          | 64 MB        |
| RLIMIT_AS    | RLIM_INFINITY              | RLIM_INFINITY |
| RLIMIT_DATA  | RLIM_INFINITY              | RLIM_INFINITY |
| RLIMIT_NOFILE| 16                         | 64           |

(`RLIMIT_NOFILE` replaces the existing `rlim_nofile` int field. All callers
of `proc_fd_limit` consult the new rlimit.)

Syscalls:

- `sys_getrlimit(int resource, struct rlimit *rlim)` — copyout.
- `sys_setrlimit(int resource, const struct rlimit *rlim)` — copyin; require
  `rlim_cur ≤ rlim_max`; allow raising `rlim_max` (single-user OS — documented
  deviation from POSIX which gates this on CAP_SYS_RESOURCE); unsupported
  resource id → `-EINVAL`.

`fork`: child copies parent's `rlim[]` array. `execve`: rlimits preserved.

`user_page_fault` stack-grow path: replace `MAX_STACK_PAGES * PAGE_SIZE` with
`p->rlim[RLIMIT_STACK].rlim_cur` (interpreted as max stack-VMA size in bytes).

### Kernel: mmap / munmap

`sys_mmap`:

- Drop `rlim_npages` enforcement.
- Add `MAP_FIXED` (`0x10`). When set:
  - Require `addr != 0`, page-aligned, `addr ≥ USER_TEXT_BASE`,
    `addr + len ≤ USER_STACK_TOP - p->rlim[RLIMIT_STACK].rlim_cur` (i.e. does
    not collide with the stack VMA range).
  - Reject (`-EINVAL`) if `[addr, addr+len)` overlaps an existing VMA.
    (POSIX permits replacement; we keep it strict for safety.)
  - Skip the top-down search; install the VMA at exactly `addr`.
- Without `MAP_FIXED`: top-down search inside `MMAP_BASE..MMAP_END` (existing
  logic, just with the new wider window).
- If `RLIMIT_AS` is finite: enforce `(sum_of_VMA_lengths + len) ≤ rlim_cur`.
  Skip when `RLIM_INFINITY`.

`sys_munmap`:

- Existing anon partial-unmap via `vma_split` + `uvmunmap_range` works for
  the new layout unchanged.
- Existing file-VMA whole-only restriction stays.

`sys_sbrk`:

- Stays for backward compatibility. Drop `rlim_npages` check.
- Decouple `HEAP_MAX` from `MMAP_BASE` (current `#define HEAP_MAX MMAP_START`
  must be removed). Pin `HEAP_MAX = 0x2000_0000UL` (512 MB) as a fixed
  legacy sbrk ceiling. Lies well below `HEAP_ARENA_BASE` (4 GB) so cannot
  collide with the malloc arena.

### Kernel: page-fault handler

- Anon path unchanged (already lazy-faults zero pages).
- Heap-VMA path unchanged.
- Stack-grow path: read soft limit from `p->rlim[RLIMIT_STACK].rlim_cur`.
- File path unchanged.

### Kernel: process exit

`proc_exit_current` already calls `vma_list_free` and frees PTEs + physical
pages via `free_user_pgtable`. No new exit hook. With slab VMAs, slot release
happens through the existing `vma_free` path called from `vma_list_free`.

### libc: malloc

Rewrite `libc/malloc.c`. Drop sbrk usage; drop `MAX_ALLOC`; drop `MIN_ALLOC`.

State:

```c
static char  *arena_base;     /* HEAP_ARENA_BASE on first init */
static char  *arena_top;      /* bump pointer */
static char  *arena_end;      /* arena_base + ARENA_SIZE */
static int    arena_inited;
static struct chunk *free_list;
```

Init (lazy on first `malloc`/`calloc`):

```c
arena_base = mmap((void *)HEAP_ARENA_BASE, ARENA_SIZE,
                  PROT_READ|PROT_WRITE,
                  MAP_PRIVATE|MAP_ANON|MAP_FIXED, -1, 0);
arena_top  = arena_base;
arena_end  = arena_base + ARENA_SIZE;
```

`ARENA_SIZE` = `HEAP_ARENA_END - HEAP_ARENA_BASE` (60 GB virtual; lazy).

Chunk header:

```c
struct chunk {
    unsigned long size;       /* low bit = USED, bit 1 = DIRECT */
    struct chunk *next;       /* free-list link when free */
};
#define USED_BIT     1UL
#define DIRECT_BIT   2UL
#define DIRECT_THRESHOLD  (16UL * 1024 * 1024)   /* 16 MB */
```

`malloc(n)`:

1. Align `n` to `ALIGN = 16`.
2. If `n + HDR_SIZE >= DIRECT_THRESHOLD`: direct path — `mmap(NULL, n+HDR,
   ...)` returns a fresh region; chunk header at the start with `DIRECT_BIT`
   set. Return `payload`.
3. Else arena path — first call inits arena via fixed mmap. Best-fit walk
   of `free_list`. If no fit, bump `arena_top` by `n + HDR_SIZE`. If
   `arena_top > arena_end`: return NULL. (Future scope: extend arena with
   another mmap; current 60 GB makes this unreachable in practice.)
4. Split residual onto free-list when remainder ≥ `HDR_SIZE + ALIGN`.

`free(p)`:

- Read header. If `DIRECT_BIT`: `munmap(p - HDR, size + HDR)` — physical
  pages return to OS immediately.
- Else clear `USED_BIT`, insert into sorted free-list, coalesce with
  neighbors. (No physical reclaim from arena.)

`realloc(p, n)`: if shrink or current chunk has trailing free space,
in-place. Else malloc + `memcpy` + free.

`calloc(nmemb, n)`: overflow-check, malloc. For arena-served chunks
`memset` zero (chunk may have been previously used). For direct-mmap
chunks skip memset (kernel guarantees zero-fill on first fault).

### libc: rlimit

Add `libc/include/sys/resource.h`:

```c
typedef unsigned long rlim_t;
struct rlimit { rlim_t rlim_cur; rlim_t rlim_max; };
#define RLIM_INFINITY  ((rlim_t)-1)
#define RLIMIT_DATA    2
#define RLIMIT_STACK   3
#define RLIMIT_NOFILE  7
#define RLIMIT_AS      9
int getrlimit(int resource, struct rlimit *rlim);
int setrlimit(int resource, const struct rlimit *rlim);
```

Wire syscall stubs in `libc/syscall.c`. Pick syscall numbers in the
existing range (`SYS_getrlimit`, `SYS_setrlimit`).

### libc: mmap flag

Add `MAP_FIXED 0x10` to `libc/include/sys/mman.h` (or wherever
existing `MAP_PRIVATE`/`MAP_ANON` live).

## Removed / deprecated

- libc malloc: `sbrk()` calls, `MAX_ALLOC`, `MIN_ALLOC`, header constant
  `MIN_ALLOC` rounding.
- Kernel: `rlim_npages`, `rlim_nvma`, `rlim_nofile` (replaced), `MAX_STACK_PAGES`,
  `HEAP_MAX` retained only as legacy sbrk ceiling, static `vma_pool`.
- The `proc_vma_total_pages()` helper.

## Backward compatibility

- `sys_sbrk` retained; legacy heap_vma still set up by exec for sbrk callers.
- All existing user programs (echo, shell, fork demo, mmap-file demos) keep
  working: text/stack ranges unchanged at low addresses; mmap window moved
  but still discovered via search by `sys_mmap`.

## Testing

### New user tests (under `bin/`)

**Lazy reservation:**
- `lazy_reserve` — `malloc(4 GiB)`, touch first/last byte, verify.
- `huge_reserve_no_touch` — `malloc(32 GiB)`, free, exit. No OOM.
- `nohuge_oom` — repeated `malloc + memset` until NULL; verify graceful.

**Correctness:**
- `pattern_write` — 256 MB malloc, write+verify pattern at every page.
- `multi_alloc_disjoint` — 1000 mallocs varied sizes, unique patterns; no
  cross-contamination.
- `realloc_grow` — 4 KB → 4 MB → 64 MB; contents preserved.
- `calloc_zero` — `calloc(1, 1 MB)`; every byte zero.

**Free + reuse:**
- `free_reuse` — alloc, free, alloc same size; arena does not grow.
- `free_coalesce` — interleaved alloc/free hitting all coalesce paths.
- `free_then_huge` — many small frees feed a later large malloc.

**Direct path:**
- `direct_threshold` — alloc just above and just below threshold; verify
  large free immediately reduces RSS.

**rlimit:**
- `stack_default` — recurse to fault; verify near 8 MB.
- `stack_raise` — `setrlimit(RLIMIT_STACK, {64 MB, 64 MB})`, recurse to
  ~32 MB, OK.
- `stack_lower` — lower soft limit; fault sooner.
- `getrlimit_defaults` — verify all defaults match spec table.
- `setrlimit_invalid` — `rlim_cur > rlim_max` → `EINVAL`; bad resource id →
  `EINVAL`.

**mmap explicit:**
- `mmap_anon_huge` — `mmap(NULL, 16 GB, ...)`, sparse touch, munmap.
- `mmap_fixed` — `MAP_FIXED` at chosen address inside mmap window;
  overlapping fixed call fails; addr below `USER_TEXT_BASE` rejected;
  addr colliding with stack reservation rejected.
- `mmap_file_regression` — existing file mmap tests untouched.

**Fork / exec:**
- `fork_cow_heap` — fork after malloc, child writes, parent unchanged.
- `exec_resets_heap` — `execve` discards arena; new program starts clean.
- `rlimit_inherit` — child inherits parent's rlimits.

**VMA slab scaling:**
- `many_mmaps` — 10 000 small mmaps each a separate VMA; all succeed; then
  munmap all; system stable.

### Kernel selftests

Extend `kernel/selftest.c`:

- VMA slab allocates more than 256 vmas without failure.
- `setrlimit`/`getrlimit` round-trip on a kernel test process.
- Page-fault stack growth uses dynamic limit, not hard-coded constant.

### Regression

All existing kernel selftests, all existing user programs (`bin/echo`,
shell, pipes, signals, page-cache mmap tests) must pass.

## Risks

- **VMA slab growth under memory pressure:** if `page_alloc()` returns NULL
  while filling slab, `vma_alloc()` returns NULL, propagating to mmap/sbrk.
  Already handled by existing NULL checks at call sites; verify.
- **MAP_FIXED collision with stack:** stack starts at `USER_STACK_TOP` and
  grows down up to `RLIMIT_STACK`. mmap with MAP_FIXED into that band
  rejected by upper-bound check.
- **Free-list scaling:** with arena reserved at 60 GB virtual but bump only,
  free-list never huge in practice. Best-fit linear scan acceptable for
  course OS scope.
- **realloc-shrink physical retention:** shrinking a direct-mmap allocation
  with `realloc` always allocates fresh + copies (no in-place shrink for
  direct chunks); old physical returns via the munmap inside `free`.

## Future scope (explicit non-goals; recorded for later)

- `madvise(MADV_DONTNEED)` so in-arena `free()` returns physical pages.
- Multi-arena (per-thread) malloc.
- Arena extension via additional mmap calls when first arena exhausted.
- Resident-page `RLIMIT_RSS` accounting.
- Demand-paged ELF loading.
- Slab-page reclaim.
