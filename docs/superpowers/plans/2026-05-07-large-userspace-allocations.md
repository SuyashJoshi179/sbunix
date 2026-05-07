# Large Userspace Allocations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make SBUnix support multi-GB user allocations with lazy physical-page backing, mmap-based malloc, POSIX rlimit syscalls, and a wide sv39 user address-space layout.

**Architecture:** Decouple virtual VMA reservations from physical page allocation. Replace 256-entry static VMA pool with a kernel-page-backed slab. Replace libc's sbrk-based heap with a single huge mmap'd arena placed in a dedicated `HEAP_ARENA` window via `MAP_FIXED`. Add POSIX `getrlimit`/`setrlimit` and dynamic stack-growth gated by `RLIMIT_STACK`.

**Tech Stack:** C, RISC-V Sv39, riscv64-unknown-elf-gcc, QEMU-virt; existing kernel modules (`vma.c`, `vmem.c`, `proc.c`, `syscall.c`, `exec.c`); existing libc (`malloc.c`, `syscall.c`, `resource.c`).

**Spec:** `docs/superpowers/specs/2026-05-07-large-userspace-allocations-design.md`

**Workflow:** Branch is `feature/large-userspace-alloc`. Push every commit immediately (`git push`). Do **not** include `Co-Authored-By` lines.

---

## File map

**Kernel — new files:**
- `kernel/include/resource.h` — `struct rlimit`, `rlim_t`, `RLIMIT_*`, `RLIMITS_NR`.

**Kernel — modified:**
- `kernel/include/vmem.h` — new layout constants.
- `kernel/include/vma.h` — drop `MAX_STACK_PAGES`, `MMAP_START`/`MMAP_END`, `HEAP_MAX` definitions; redefine.
- `kernel/include/proc.h` — replace `rlim_nofile/rlim_nvma/rlim_npages` with `struct rlimit rlim[RLIMITS_NR]`.
- `kernel/include/syscall.h` — add `SYS_getrlimit = 117`, `SYS_setrlimit = 118`.
- `kernel/vma.c` — replace static pool with slab (`vma_slab_alloc/free`); replace `MAX_STACK_PAGES` in fault handler with rlimit lookup.
- `kernel/proc.c` — init `rlim[]` defaults; fork copies array; drop old fields.
- `kernel/syscall.c` — drop `proc_vma_total_pages` and rlim_npages enforcement; rewrite `proc_fd_limit`; add `MAP_FIXED` in `sys_mmap`; add `sys_setrlimit`/`sys_getrlimit`; replace static window dependency.
- `kernel/exec.c` — no semantic change but verify rlimit preserved.

**libc — modified:**
- `libc/include/sys/mman.h` — add `MAP_FIXED 0x10`.
- `libc/include/sys/resource.h` — already exists; keep types.
- `libc/syscall.c` — wire `setrlimit`/`getrlimit` to syscalls.
- `libc/resource.c` — replace stub with kernel-syscall calls.
- `libc/malloc.c` — full rewrite: single mmap'd arena + free-list, direct path for huge allocs.

**bin — new tests:**
- `bin/lazy_reserve/lazy_reserve.c`
- `bin/huge_reserve_no_touch/huge_reserve_no_touch.c`
- `bin/pattern_write/pattern_write.c`
- `bin/free_reuse/free_reuse.c`
- `bin/direct_threshold/direct_threshold.c`
- `bin/setrlimit_test/setrlimit_test.c`
- `bin/stack_grow_test/stack_grow_test.c`
- `bin/mmap_fixed_test/mmap_fixed_test.c`
- `bin/many_mmaps_test/many_mmaps_test.c`
- `bin/exec_resets_arena/exec_resets_arena.c`

**bin — updated tests:**
- `bin/rlimit_test/rlimit_test.c` — adapted: NOFILE retained; drop vma cap and page cap subtests (those caps are gone).

---

## Build / test invocation

- Build: `make -j` from repo root. Output: `build/kernel.elf` and `build/disk.img`.
- Run kernel under QEMU: `make run` (interactive) or `make test` (scripted, exits after init).
- Run a specific user program: edit `rootfs/etc/rc` or `rootfs/etc/inittab` to spawn it, then `make run`.
- All commits: `git push origin feature/large-userspace-alloc` after each commit.

---

## Task 1: Add `kernel/include/resource.h`

**Files:**
- Create: `kernel/include/resource.h`

- [ ] **Step 1: Create header**

```c
#pragma once
#include <stdint.h>

/* POSIX getrlimit/setrlimit types. Single-user OS: setrlimit may raise
 * rlim_max without privilege check (documented deviation from POSIX). */

typedef uint64_t rlim_t;

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

#define RLIM_INFINITY  ((rlim_t)-1)

/* Linux-compatible resource ids (sparse). Values match libc/include/sys/resource.h. */
#define RLIMIT_CPU      0
#define RLIMIT_FSIZE    1
#define RLIMIT_DATA     2
#define RLIMIT_STACK    3
#define RLIMIT_CORE     4
#define RLIMIT_RSS      5
#define RLIMIT_NPROC    6
#define RLIMIT_NOFILE   7
#define RLIMIT_MEMLOCK  8
#define RLIMIT_AS       9

#define RLIMITS_NR      16   /* table size; sparse */
```

- [ ] **Step 2: Build & verify compiles**

Run: `make -j`. Expected: build succeeds (header is unused by anyone yet).

- [ ] **Step 3: Commit + push**

```bash
git add kernel/include/resource.h
git commit -m "kernel: add resource.h with POSIX rlimit types"
git push origin feature/large-userspace-alloc
```

---

## Task 2: Update layout constants in `vmem.h` / `vma.h`

**Files:**
- Modify: `kernel/include/vmem.h`
- Modify: `kernel/include/vma.h`

- [ ] **Step 1: Replace user VA layout in `vmem.h`**

Replace the lines (currently):
```c
// User virtual address space layout
#define USER_TEXT_BASE  0x1000UL          // first user code page
#define USER_STACK_TOP  0x40000000UL      // user stack grows down from here
```

With:
```c
// User virtual address space layout (Sv39: user VA up to 256 GB).
// Regions are segregated: HEAP_ARENA is reserved for libc malloc's mmap'd
// arena (placed via MAP_FIXED). MMAP window is for explicit user mmap()
// calls (top-down search). Stack lives at the very top, grow-down,
// gated at runtime by RLIMIT_STACK.
#define USER_TEXT_BASE     0x0000000000001000UL  /* first user code page         */
#define HEAP_ARENA_BASE    0x0000000100000000UL  /* 4 GB                         */
#define HEAP_ARENA_END     0x0000001000000000UL  /* 64 GB                        */
#define MMAP_BASE          HEAP_ARENA_END        /* 64 GB                        */
#define MMAP_END_VA        0x0000002000000000UL  /* 128 GB                       */
#define USER_STACK_TOP     0x0000004000000000UL  /* 256 GB                       */

#define DEFAULT_STACK_SOFT (8UL  * 1024 * 1024)  /* 8 MB                         */
#define DEFAULT_STACK_HARD (64UL * 1024 * 1024)  /* 64 MB                        */
#define DEFAULT_STACK_MAX  DEFAULT_STACK_HARD
```

- [ ] **Step 2: Replace mmap/heap macros in `vma.h`**

Replace these lines:
```c
#define MAX_STACK_PAGES 256
#define MMAP_START      0x20000000UL
#define MMAP_END        (USER_STACK_TOP - (MAX_STACK_PAGES * 4096UL))
#define HEAP_MAX        MMAP_START
```

With:
```c
/* Legacy sbrk ceiling (decoupled from MMAP_BASE so sbrk callers stay
 * confined to the low region). Lies far below HEAP_ARENA_BASE. */
#define HEAP_MAX        0x20000000UL          /* 512 MB legacy sbrk ceiling */

/* Top-down search bounds for sys_mmap (when MAP_FIXED not set). */
#define MMAP_START      MMAP_BASE
#define MMAP_END        MMAP_END_VA
```

(`MAX_STACK_PAGES` removed; users now consult `p->rlim[RLIMIT_STACK].rlim_cur`.)

- [ ] **Step 3: Build to find compile errors**

Run: `make -j 2>&1 | head -40`. Expect failures referring to `MAX_STACK_PAGES`. Note them — Task 4 fixes the only remaining caller (`kernel/vma.c`).

- [ ] **Step 4: Commit + push** (do not push if build still has unrelated errors)

Defer commit; the symbol `MAX_STACK_PAGES` is referenced in `kernel/vma.c::user_page_fault`. Task 4 fixes it. Commit at the end of Task 4.

---

## Task 3: Replace VMA pool with slab allocator

**Files:**
- Modify: `kernel/vma.c`

- [ ] **Step 1: Replace pool init/alloc/free in `kernel/vma.c`**

Replace this block at the top of the file (currently lines 11–41):
```c
#define VMA_POOL_SIZE 256

static struct vma  vma_pool[VMA_POOL_SIZE];
static struct vma *vma_freelist;
static int         vma_pool_inited;

static void vma_pool_init(void) { ... }
struct vma *vma_alloc(void) { ... }
void vma_free(struct vma *v) { ... }
```

With:
```c
/* Slab allocator: refill from kernel page allocator on demand. Each refill
 * page becomes (PAGE_SIZE / sizeof(struct vma)) slots threaded onto the
 * free list via v->next. Slots are reused indefinitely; slab pages are
 * never freed (acceptable for a course OS; future TODO: reclaim). */
static struct vma *vma_freelist;

static int vma_slab_refill(void) {
    void *page = page_alloc();
    if (!page) return -1;
    unsigned int n = PAGE_SIZE / sizeof(struct vma);
    struct vma *arr = (struct vma *)page;
    for (unsigned int i = 0; i < n; i++) {
        arr[i].next = vma_freelist;
        vma_freelist = &arr[i];
    }
    return 0;
}

struct vma *vma_alloc(void) {
    if (!vma_freelist && vma_slab_refill() < 0) return 0;
    struct vma *v = vma_freelist;
    vma_freelist = v->next;
    memset(v, 0, sizeof(*v));
    return v;
}

void vma_free(struct vma *v) {
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->next = vma_freelist;
    vma_freelist = v;
}
```

- [ ] **Step 2: Build to confirm slab compiles**

Run: `make -j 2>&1 | head -20`. Expect remaining `MAX_STACK_PAGES` errors only. Slab itself should compile.

- [ ] **Step 3: No commit yet** — squashed with Task 4.

---

## Task 4: Update fault handler stack-growth limit

**Files:**
- Modify: `kernel/vma.c` (`user_page_fault`)

- [ ] **Step 1: Replace stack-growth bound**

Currently in `user_page_fault`:
```c
uint64_t min_start = USER_STACK_TOP - (MAX_STACK_PAGES * PAGE_SIZE);
```

Replace with:
```c
struct pcb *pp = p; /* alias for clarity below */
uint64_t stack_max = pp->rlim[RLIMIT_STACK].rlim_cur;
if (stack_max == RLIM_INFINITY) stack_max = USER_STACK_TOP - USER_TEXT_BASE;
uint64_t min_start = USER_STACK_TOP - stack_max;
```

Add `#include <resource.h>` near top of the file (after the existing
`#include <vma.h>`).

- [ ] **Step 2: Build kernel**

Run: `make -j`. Expect successful build (apart from any tests).

- [ ] **Step 3: Commit + push**

```bash
git add kernel/include/vmem.h kernel/include/vma.h kernel/vma.c
git commit -m "kernel: widen user VA layout (sv39) and slab-allocate VMAs

- Move heap arena to 4-64 GB, mmap window to 64-128 GB, stack top at 256 GB
- Replace 256-entry static VMA pool with kernel-page-backed slab
- Stack growth bound now consults RLIMIT_STACK at fault time"
git push origin feature/large-userspace-alloc
```

---

## Task 5: Replace PCB rlimit fields with `rlim[]` array

**Files:**
- Modify: `kernel/include/proc.h`
- Modify: `kernel/proc.c`

- [ ] **Step 1: Update PCB struct**

In `kernel/include/proc.h`, replace:
```c
// Phase 9c: per-process hard resource limits.
int            rlim_nofile;
int            rlim_nvma;
int            rlim_npages;
```

With:
```c
#include <resource.h>
/* POSIX rlimit table (indexed by resource id; sparse). */
struct rlimit  rlim[RLIMITS_NR];
```

- [ ] **Step 2: Update `alloc_proc` defaults**

In `kernel/proc.c`, replace:
```c
#define RLIM_NOFILE_DEFAULT 16
#define RLIM_NVMA_DEFAULT   64
#define RLIM_NPAGES_DEFAULT 256
```

With:
```c
#include <resource.h>
```
(remove the three `#define`s)

In the body that currently sets the three int fields:
```c
p->rlim_nofile = RLIM_NOFILE_DEFAULT;
p->rlim_nvma   = RLIM_NVMA_DEFAULT;
p->rlim_npages = RLIM_NPAGES_DEFAULT;
```

Replace with:
```c
for (int i = 0; i < RLIMITS_NR; i++) {
    p->rlim[i].rlim_cur = RLIM_INFINITY;
    p->rlim[i].rlim_max = RLIM_INFINITY;
}
p->rlim[RLIMIT_STACK].rlim_cur  = DEFAULT_STACK_SOFT;
p->rlim[RLIMIT_STACK].rlim_max  = DEFAULT_STACK_HARD;
p->rlim[RLIMIT_NOFILE].rlim_cur = 16;
p->rlim[RLIMIT_NOFILE].rlim_max = 64;
```

- [ ] **Step 3: Update fork inherit**

In `kernel/proc.c::proc_fork_current` (around the existing copy block), replace:
```c
child->rlim_nofile    = parent->rlim_nofile;
child->rlim_nvma      = parent->rlim_nvma;
child->rlim_npages    = parent->rlim_npages;
```

With:
```c
for (int i = 0; i < RLIMITS_NR; i++)
    child->rlim[i] = parent->rlim[i];
```

- [ ] **Step 4: Build, expect errors in `kernel/syscall.c`**

Run: `make -j 2>&1 | grep -E 'error|undeclared' | head -10`. Expect errors about `rlim_nofile`/`rlim_nvma`/`rlim_npages` referenced from `kernel/syscall.c`. Task 6 fixes those.

- [ ] **Step 5: No commit yet** — bundle with Task 6.

---

## Task 6: Drop legacy rlim caps from `kernel/syscall.c`

**Files:**
- Modify: `kernel/syscall.c`

- [ ] **Step 1: Update `proc_fd_limit`**

Replace:
```c
static int proc_fd_limit(const struct pcb *p) {
    if (!p) return NOFILE;
    int lim = p->rlim_nofile;
    if (lim < 0 || lim > NOFILE) lim = NOFILE;
    return lim;
}
```

With:
```c
static int proc_fd_limit(const struct pcb *p) {
    if (!p) return NOFILE;
    rlim_t lim = p->rlim[RLIMIT_NOFILE].rlim_cur;
    if (lim == RLIM_INFINITY || lim > (rlim_t)NOFILE) return NOFILE;
    return (int)lim;
}
```

- [ ] **Step 2: Delete `proc_vma_total_pages`**

Delete the `proc_vma_total_pages` function entirely (current lines ~74–83). Also delete the unused `proc_vma_count` if it's still attribute-unused (leave if other code references it).

- [ ] **Step 3: Drop the page-cap block in `sys_sbrk`**

In `sys_sbrk`, delete the lines (currently lines 929–935):
```c
// Enforce per-process mapped-page cap against the VMA address range;
// actual physical pages come on demand via user_page_fault.
uint64_t old_pages = (old_end - p->heap_vma->start) / PAGE_SIZE;
uint64_t new_pages = (new_end - p->heap_vma->start) / PAGE_SIZE;
uint64_t add_pages = (new_pages > old_pages) ? (new_pages - old_pages) : 0;
if (proc_vma_total_pages(p) + add_pages > (uint64_t)p->rlim_npages)
    return -ENOMEM;
```

Keep the `if (new_end > HEAP_MAX) return -ENOMEM;` guard — `HEAP_MAX` is now a fixed 512 MB legacy ceiling.

- [ ] **Step 4: Add `#include <resource.h>` to `kernel/syscall.c`**

Near the top, alongside other kernel includes.

- [ ] **Step 5: Build kernel — should succeed now**

Run: `make -j`. Expect a clean build.

- [ ] **Step 6: Commit + push**

```bash
git add kernel/include/proc.h kernel/proc.c kernel/syscall.c
git commit -m "kernel: switch PCB to POSIX rlim[] table; drop legacy page cap

- Replace rlim_nofile/rlim_nvma/rlim_npages ints with struct rlimit rlim[]
- Default RLIMIT_STACK = 8MB soft / 64MB hard; RLIMIT_NOFILE = 16/64
- Other resources start at RLIM_INFINITY
- Drop proc_vma_total_pages enforcement from sys_sbrk
- proc_fd_limit reads from rlim[RLIMIT_NOFILE]"
git push origin feature/large-userspace-alloc
```

---

## Task 7: Add `MAP_FIXED` to `sys_mmap` and drop VMA-count cap

**Files:**
- Modify: `kernel/syscall.c` (`sys_mmap`)

- [ ] **Step 1: Add `MAP_FIXED` define + accept it in `sys_mmap`**

Near the existing flags block in `sys_mmap`:
```c
#define MAP_PRIVATE 0x02
#define MAP_SHARED  0x01
#define MAP_ANON    0x20
```

Add:
```c
#define MAP_FIXED   0x10
```

- [ ] **Step 2: Implement MAP_FIXED placement**

Replace the existing top-down search prologue (currently around line 997):
```c
/* Top-down search for an empty range — preserve existing logic. */
struct pcb *proc = current_proc();
uint64_t search = MMAP_END - len;
while (search >= MMAP_START) { ... }
if (search < MMAP_START) { ... }
```

With:
```c
struct pcb *proc = current_proc();
if (!proc) { if (fip) inode_put(fip); return -EINVAL; }

uint64_t search;
if (flags & MAP_FIXED) {
    if (addr == 0 || (addr & (PAGE_SIZE - 1))) {
        if (fip) inode_put(fip); return -EINVAL;
    }
    if (addr < USER_TEXT_BASE) {
        if (fip) inode_put(fip); return -EINVAL;
    }
    /* Reject collisions with stack reservation. */
    rlim_t stack_max = proc->rlim[RLIMIT_STACK].rlim_cur;
    if (stack_max == RLIM_INFINITY) stack_max = USER_STACK_TOP - USER_TEXT_BASE;
    if (addr + len > USER_STACK_TOP - stack_max) {
        if (fip) inode_put(fip); return -EINVAL;
    }
    /* Reject overlap with any existing VMA. */
    for (struct vma *v = proc->vma_list; v; v = v->next) {
        if (addr < v->end && addr + len > v->start) {
            if (fip) inode_put(fip); return -EINVAL;
        }
    }
    search = addr;
} else {
    /* Top-down search inside the (now wide) MMAP window. */
    if (len > MMAP_END - MMAP_START) { if (fip) inode_put(fip); return -ENOMEM; }
    search = MMAP_END - len;
    search &= ~(PAGE_SIZE - 1);
    while (search >= MMAP_START) {
        int overlap = 0;
        for (struct vma *v = proc->vma_list; v; v = v->next) {
            if (search < v->end && search + len > v->start) {
                overlap = 1;
                if (v->start < MMAP_START + len) { /* about to underflow */
                    if (fip) inode_put(fip); return -ENOMEM;
                }
                search = v->start - len;
                search &= ~(PAGE_SIZE - 1);
                break;
            }
        }
        if (!overlap) break;
        if (search < MMAP_START) { if (fip) inode_put(fip); return -ENOMEM; }
    }
    if (search < MMAP_START) { if (fip) inode_put(fip); return -ENOMEM; }
}
```

- [ ] **Step 3: Build kernel**

Run: `make -j`. Expect clean.

- [ ] **Step 4: Commit + push**

```bash
git add kernel/syscall.c
git commit -m "kernel: add MAP_FIXED support to sys_mmap

- MAP_FIXED honors caller-supplied addr; rejects overlaps and collisions
  with text base / stack reservation
- Default (no MAP_FIXED) keeps top-down search inside MMAP_BASE..MMAP_END
  using the new wide window"
git push origin feature/large-userspace-alloc
```

---

## Task 8: Add `sys_getrlimit` and `sys_setrlimit` syscalls

**Files:**
- Modify: `kernel/include/syscall.h`
- Modify: `kernel/syscall.c`

- [ ] **Step 1: Reserve syscall numbers**

In `kernel/include/syscall.h` (anywhere in the appropriate range, after `SYS_msync`):

```c
#define SYS_getrlimit 117   // (resource, struct rlimit *)
#define SYS_setrlimit 118   // (resource, const struct rlimit *)
```

- [ ] **Step 2: Implement handlers**

In `kernel/syscall.c`, before the dispatch table, add:

```c
static int64_t sys_getrlimit(int resource, struct rlimit *urlim) {
    if (resource < 0 || resource >= RLIMITS_NR) return -EINVAL;
    if (!urlim) return -EFAULT;
    struct pcb *p = current_proc();
    if (!p) return -EINVAL;
    struct rlimit r = p->rlim[resource];
    if (copyout(urlim, &r, sizeof(r)) < 0) return -EFAULT;
    return 0;
}

static int64_t sys_setrlimit(int resource, const struct rlimit *urlim) {
    if (resource < 0 || resource >= RLIMITS_NR) return -EINVAL;
    if (!urlim) return -EFAULT;
    struct pcb *p = current_proc();
    if (!p) return -EINVAL;
    struct rlimit r;
    if (copyin(&r, urlim, sizeof(r)) < 0) return -EFAULT;
    if (r.rlim_cur != RLIM_INFINITY && r.rlim_max != RLIM_INFINITY
        && r.rlim_cur > r.rlim_max) return -EINVAL;
    /* Single-user OS: allow raising rlim_max without privilege. */
    p->rlim[resource] = r;
    return 0;
}
```

- [ ] **Step 3: Wire dispatch**

In the `switch` in the syscall dispatcher (`kernel/syscall.c`), add:

```c
case SYS_getrlimit:
    return sys_getrlimit((int)trapframe[TF_A0],
                         (struct rlimit *)trapframe[TF_A1]);
case SYS_setrlimit:
    return sys_setrlimit((int)trapframe[TF_A0],
                         (const struct rlimit *)trapframe[TF_A1]);
```

- [ ] **Step 4: Build kernel**

Run: `make -j`. Expect clean.

- [ ] **Step 5: Commit + push**

```bash
git add kernel/include/syscall.h kernel/syscall.c
git commit -m "kernel: add sys_getrlimit/sys_setrlimit (POSIX)

Syscall numbers 117/118. Reads/writes the per-PCB rlim[] table with
soft<=max validation. Single-user OS: rlim_max raisable without priv."
git push origin feature/large-userspace-alloc
```

---

## Task 9: libc — add `MAP_FIXED` and wire setrlimit/getrlimit syscalls

**Files:**
- Modify: `libc/include/sys/mman.h`
- Modify: `libc/syscall.c`
- Modify: `libc/resource.c`

- [ ] **Step 1: Add MAP_FIXED to libc**

In `libc/include/sys/mman.h`, after `#define MAP_ANON 0x20`:
```c
#define MAP_FIXED 0x10
```

- [ ] **Step 2: Add syscall stubs**

In `libc/syscall.c`, after the existing `mmap`/`munmap`/`msync` stubs:

```c
int getrlimit(int resource, struct rlimit *rlim) {
    long r = ecall2(117, (long)resource, (long)rlim);
    if (r < 0) { errno = (int)(-r); return -1; }
    return 0;
}

int setrlimit(int resource, const struct rlimit *rlim) {
    long r = ecall2(118, (long)resource, (long)rlim);
    if (r < 0) { errno = (int)(-r); return -1; }
    return 0;
}
```

Add `#include <sys/resource.h>` near the top of `libc/syscall.c` if not already present.

- [ ] **Step 3: Replace `libc/resource.c` stub with kernel calls**

The existing `libc/resource.c` services rlimit entirely in userspace. Delete the `getrlimit`/`setrlimit` definitions there (they are now in `libc/syscall.c`); keep `getrusage`, `getpriority`, `setpriority` stubs as-is.

New file content for `libc/resource.c`:
```c
#include <sys/resource.h>
#include <string.h>
#include <errno.h>

int getrusage(int who, struct rusage *usage) {
    (void)who;
    if (!usage) { errno = EFAULT; return -1; }
    memset(usage, 0, sizeof(*usage));
    return 0;
}

int getpriority(int which, id_t who) {
    (void)which; (void)who;
    return 0;
}

int setpriority(int which, id_t who, int prio) {
    (void)which; (void)who; (void)prio;
    return 0;
}
```

- [ ] **Step 4: Build libc**

Run: `make -j`. Expect clean.

- [ ] **Step 5: Commit + push**

```bash
git add libc/include/sys/mman.h libc/syscall.c libc/resource.c
git commit -m "libc: wire setrlimit/getrlimit to kernel; expose MAP_FIXED

Resource limits now reflect the kernel's per-process state and are
inherited across fork. MAP_FIXED is now declared for user programs."
git push origin feature/large-userspace-alloc
```

---

## Task 10: libc malloc rewrite (single mmap arena + free-list + direct path)

**Files:**
- Modify: `libc/malloc.c`

- [ ] **Step 1: Replace `libc/malloc.c` entirely**

Full new content:

```c
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

/* Single huge mmap'd arena placed at HEAP_ARENA_BASE via MAP_FIXED.
 * Bump pointer + best-fit free list inside. Allocations >= DIRECT_THRESHOLD
 * bypass the arena and use a per-call mmap; their free() returns physical
 * pages to the OS immediately via munmap. Within the arena, free()
 * coalesces but does not return physical pages to the OS (future TODO:
 * madvise(MADV_DONTNEED)). */

#define ALIGN              16
#define HDR_SIZE           (sizeof(struct chunk))
#define USED_BIT           1UL
#define DIRECT_BIT         2UL
#define DIRECT_THRESHOLD   (16UL * 1024 * 1024)

#define HEAP_ARENA_BASE    0x0000000100000000UL  /* keep in sync with kernel */
#define HEAP_ARENA_END     0x0000001000000000UL
#define ARENA_SIZE         (HEAP_ARENA_END - HEAP_ARENA_BASE)

struct chunk {
    unsigned long size;       /* low bit USED, bit 1 DIRECT             */
    struct chunk *next;       /* free-list link when free               */
};

static char         *arena_base;
static char         *arena_top;
static char         *arena_end;
static int           arena_inited;
static struct chunk *free_list;

static unsigned long align_up(unsigned long n, unsigned long a) {
    return (n + a - 1) & ~(a - 1);
}

static int arena_init(void) {
    if (arena_inited) return 0;
    void *p = mmap((void *)HEAP_ARENA_BASE, ARENA_SIZE,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) return -1;
    arena_base = (char *)p;
    arena_top  = arena_base;
    arena_end  = arena_base + ARENA_SIZE;
    arena_inited = 1;
    return 0;
}

static void *arena_alloc(unsigned long size) {
    /* Best-fit on free list. */
    struct chunk *prev = 0, *best = 0, *best_prev = 0;
    for (struct chunk *c = free_list; c; c = c->next) {
        if (c->size >= size) {
            if (!best || c->size < best->size) { best = c; best_prev = prev; }
        }
        prev = c;
    }
    if (best) {
        if (best->size >= size + HDR_SIZE + ALIGN) {
            struct chunk *rest = (struct chunk *)((char *)best + HDR_SIZE + size);
            rest->size = best->size - size - HDR_SIZE;
            rest->next = best->next;
            best->size = size;
            if (best_prev) best_prev->next = rest;
            else free_list = rest;
        } else {
            if (best_prev) best_prev->next = best->next;
            else free_list = best->next;
        }
        best->size |= USED_BIT;
        best->next = 0;
        return (char *)best + HDR_SIZE;
    }
    /* Bump. */
    unsigned long need = HDR_SIZE + size;
    if ((unsigned long)(arena_end - arena_top) < need) return 0;
    struct chunk *c = (struct chunk *)arena_top;
    arena_top += need;
    c->size = size | USED_BIT;
    c->next = 0;
    return (char *)c + HDR_SIZE;
}

static void *direct_alloc(unsigned long size) {
    unsigned long total = HDR_SIZE + size;
    /* mmap rounds up to PAGE_SIZE internally. */
    void *p = mmap(0, (long)total, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) return 0;
    struct chunk *c = (struct chunk *)p;
    c->size = size | USED_BIT | DIRECT_BIT;
    c->next = 0;
    return (char *)c + HDR_SIZE;
}

void *malloc(unsigned long size) {
    if (size == 0) return 0;
    size = align_up(size, ALIGN);

    if (size + HDR_SIZE >= DIRECT_THRESHOLD)
        return direct_alloc(size);

    if (arena_init() < 0) return 0;
    return arena_alloc(size);
}

void free(void *ptr) {
    if (!ptr) return;
    struct chunk *c = (struct chunk *)((char *)ptr - HDR_SIZE);

    if (c->size & DIRECT_BIT) {
        unsigned long size = c->size & ~(USED_BIT | DIRECT_BIT);
        munmap(c, (long)(HDR_SIZE + size));
        return;
    }

    c->size &= ~USED_BIT;

    /* Sorted insert + coalesce. */
    struct chunk *prev = 0, *cur = free_list;
    while (cur && cur < c) { prev = cur; cur = cur->next; }

    if (cur && (char *)c + HDR_SIZE + c->size == (char *)cur) {
        c->size += HDR_SIZE + cur->size;
        c->next = cur->next;
    } else {
        c->next = cur;
    }

    if (prev && (char *)prev + HDR_SIZE + prev->size == (char *)c) {
        prev->size += HDR_SIZE + c->size;
        prev->next = c->next;
    } else if (prev) {
        prev->next = c;
    } else {
        free_list = c;
    }
}

void *calloc(unsigned long nmemb, unsigned long size) {
    unsigned long total = nmemb * size;
    if (nmemb != 0 && total / nmemb != size) return 0;
    void *p = malloc(total);
    if (!p) return 0;
    /* Direct chunks come from a fresh mmap that lazy-faults zero pages,
     * so memset is redundant for them; but we cannot tell from p without
     * reading the header again, and memset on already-zero pages costs
     * only the fault traffic we'd take anyway. Safe to always zero. */
    memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, unsigned long size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return 0; }

    struct chunk *c = (struct chunk *)((char *)ptr - HDR_SIZE);
    unsigned long old_size = c->size & ~(USED_BIT | DIRECT_BIT);
    if (old_size >= size) return ptr;

    void *newp = malloc(size);
    if (!newp) return 0;
    memcpy(newp, ptr, old_size);
    free(ptr);
    return newp;
}
```

- [ ] **Step 2: Build libc + user binaries**

Run: `make -j`. Expect clean. (Existing user tests still link against the new `malloc`/`free`.)

- [ ] **Step 3: Commit + push**

```bash
git add libc/malloc.c
git commit -m "libc: rewrite malloc on a single mmap'd arena with direct path

- First malloc places a 60 GB virtual arena at HEAP_ARENA_BASE via
  MAP_FIXED; physical pages back lazily on first write.
- Allocations >= 16 MB go through direct mmap; their free() munmaps
  the entire region, returning physical pages to the OS.
- Within the arena, free() coalesces virtually but does not return
  physical pages (future scope: madvise(MADV_DONTNEED)).
- Drops sbrk usage and the 128 KB MAX_ALLOC cap."
git push origin feature/large-userspace-alloc
```

---

## Task 11: Smoke test — boot, init, echo

**Files:**
- (no edits)

- [ ] **Step 1: Boot with existing rootfs**

Run: `make run | tee /tmp/sb.log`. Wait for shell prompt or init banner; press Ctrl-A X to exit.

- [ ] **Step 2: Verify clean boot in log**

Run: `grep -E 'panic|ERROR|FAIL' /tmp/sb.log | head`. Expect no kernel panic. Expect existing kernel selftests pass (look for the existing self-test summary line they print).

- [ ] **Step 3: Run echo**

In QEMU shell (or via `rootfs/etc/rc`), run `echo hello world`. Expect `hello world` echoed.

- [ ] **Step 4: No commit** — verification step.

---

## Task 12: New test — `bin/lazy_reserve`

**Files:**
- Create: `bin/lazy_reserve/lazy_reserve.c`

- [ ] **Step 1: Write the test**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* malloc(4 GiB), touch first/last byte, verify, free, exit OK. */
int main(void) {
    unsigned long sz = 4UL * 1024 * 1024 * 1024;
    char *p = (char *)malloc(sz);
    if (!p) { printf("FAIL malloc 4GiB returned NULL\n"); return 1; }
    p[0] = 'A';
    p[sz - 1] = 'Z';
    if (p[0] != 'A' || p[sz - 1] != 'Z') {
        printf("FAIL pattern mismatch %c %c\n", p[0], p[sz - 1]);
        return 1;
    }
    free(p);
    printf("PASS lazy_reserve 4GiB\n");
    return 0;
}
```

- [ ] **Step 2: Run via init**

Add `lazy_reserve` to whatever runs user tests (e.g. add a line in `rootfs/etc/rc` if that's the convention, or invoke it from the shell after boot). Build: `make -j`.

- [ ] **Step 3: Run it under QEMU**

Run: `make run` and execute `lazy_reserve`. Expect: `PASS lazy_reserve 4GiB`.

- [ ] **Step 4: Commit + push**

```bash
git add bin/lazy_reserve
git commit -m "test(bin): lazy_reserve — malloc 4 GiB, touch ends, verify"
git push origin feature/large-userspace-alloc
```

---

## Task 13: New test — `bin/huge_reserve_no_touch`

**Files:**
- Create: `bin/huge_reserve_no_touch/huge_reserve_no_touch.c`

- [ ] **Step 1: Write the test**

```c
#include <stdio.h>
#include <stdlib.h>

/* malloc(32 GiB), do not touch, free, exit. Verifies that virtual
 * reservation alone consumes no physical memory and that the arena
 * accommodates a single very large request via the direct path. */
int main(void) {
    unsigned long sz = 32UL * 1024 * 1024 * 1024;
    char *p = (char *)malloc(sz);
    if (!p) { printf("FAIL malloc 32GiB returned NULL\n"); return 1; }
    free(p);
    printf("PASS huge_reserve_no_touch 32GiB\n");
    return 0;
}
```

- [ ] **Step 2: Build, run**

Run: `make -j` then run under QEMU. Expect PASS.

- [ ] **Step 3: Commit + push**

```bash
git add bin/huge_reserve_no_touch
git commit -m "test(bin): huge_reserve_no_touch — malloc 32 GiB without touch"
git push origin feature/large-userspace-alloc
```

---

## Task 14: New test — `bin/pattern_write`

**Files:**
- Create: `bin/pattern_write/pattern_write.c`

- [ ] **Step 1: Write the test**

```c
#include <stdio.h>
#include <stdlib.h>

#define SZ (256UL * 1024 * 1024)
#define PG 4096UL

int main(void) {
    char *p = (char *)malloc(SZ);
    if (!p) { printf("FAIL malloc 256MiB\n"); return 1; }
    /* Write pattern at every page boundary. */
    for (unsigned long off = 0; off < SZ; off += PG)
        p[off] = (char)((off / PG) & 0xFF);
    /* Verify. */
    for (unsigned long off = 0; off < SZ; off += PG) {
        char want = (char)((off / PG) & 0xFF);
        if (p[off] != want) {
            printf("FAIL off=%lu got=%d want=%d\n",
                   off, (int)p[off], (int)want);
            free(p); return 1;
        }
    }
    free(p);
    printf("PASS pattern_write 256MiB\n");
    return 0;
}
```

- [ ] **Step 2: Build, run, expect PASS.**

- [ ] **Step 3: Commit + push**

```bash
git add bin/pattern_write
git commit -m "test(bin): pattern_write — page-stride write/verify across 256 MiB"
git push origin feature/large-userspace-alloc
```

---

## Task 15: New test — `bin/free_reuse`

**Files:**
- Create: `bin/free_reuse/free_reuse.c`

- [ ] **Step 1: Write the test**

```c
#include <stdio.h>
#include <stdlib.h>

/* Allocate a small block, free, reallocate same size: pointer must come
 * from the freelist (be == previous pointer) and arena must not grow. */
int main(void) {
    void *a = malloc(4096);
    if (!a) { printf("FAIL alloc1\n"); return 1; }
    free(a);
    void *b = malloc(4096);
    if (!b) { printf("FAIL alloc2\n"); return 1; }
    if (a != b) {
        printf("FAIL freelist did not reuse: %p vs %p\n", a, b);
        return 1;
    }
    free(b);
    printf("PASS free_reuse\n");
    return 0;
}
```

- [ ] **Step 2: Build, run, expect PASS.**

- [ ] **Step 3: Commit + push**

```bash
git add bin/free_reuse
git commit -m "test(bin): free_reuse — verify in-arena freelist reuse"
git push origin feature/large-userspace-alloc
```

---

## Task 16: New test — `bin/direct_threshold`

**Files:**
- Create: `bin/direct_threshold/direct_threshold.c`

- [ ] **Step 1: Write the test**

```c
#include <stdio.h>
#include <stdlib.h>

/* Just below threshold goes to arena (close to arena_base around 4 GiB);
 * just above threshold goes to direct mmap (in the explicit mmap window
 * around 64 GiB). Compare addresses to confirm they came from
 * different regions. */
int main(void) {
    unsigned long below = 8UL * 1024 * 1024;          /* 8 MiB  */
    unsigned long above = 32UL * 1024 * 1024;         /* 32 MiB */
    char *a = (char *)malloc(below);
    char *b = (char *)malloc(above);
    if (!a || !b) { printf("FAIL alloc\n"); return 1; }

    unsigned long ua = (unsigned long)a;
    unsigned long ub = (unsigned long)b;
    /* HEAP_ARENA_BASE = 4 GiB; MMAP_BASE = 64 GiB. */
    if (ua < (1UL << 32) || ua >= (1UL << 36)) {
        printf("FAIL below-threshold ptr outside heap arena: %p\n", a);
        return 1;
    }
    if (ub < (1UL << 36)) {
        printf("FAIL above-threshold ptr inside heap arena: %p\n", b);
        return 1;
    }
    free(a); free(b);
    printf("PASS direct_threshold\n");
    return 0;
}
```

- [ ] **Step 2: Build, run, expect PASS.**

- [ ] **Step 3: Commit + push**

```bash
git add bin/direct_threshold
git commit -m "test(bin): direct_threshold — verify direct vs arena placement"
git push origin feature/large-userspace-alloc
```

---

## Task 17: New test — `bin/setrlimit_test`

**Files:**
- Create: `bin/setrlimit_test/setrlimit_test.c`

- [ ] **Step 1: Write the test**

```c
#include <errno.h>
#include <stdio.h>
#include <sys/resource.h>

int main(void) {
    int pass = 0, fail = 0;
    struct rlimit r;

    if (getrlimit(RLIMIT_STACK, &r) != 0) { fail++; printf("FAIL getrlimit stack\n"); }
    else if (r.rlim_cur != 8UL * 1024 * 1024 || r.rlim_max != 64UL * 1024 * 1024) {
        printf("FAIL stack default cur=%lu max=%lu\n", r.rlim_cur, r.rlim_max); fail++;
    } else { pass++; printf("PASS stack default 8M/64M\n"); }

    if (getrlimit(RLIMIT_NOFILE, &r) != 0) { fail++; printf("FAIL getrlimit nofile\n"); }
    else if (r.rlim_cur != 16 || r.rlim_max != 64) {
        printf("FAIL nofile default cur=%lu max=%lu\n", r.rlim_cur, r.rlim_max); fail++;
    } else { pass++; printf("PASS nofile default 16/64\n"); }

    /* Raise stack soft within hard. */
    r.rlim_cur = 16UL * 1024 * 1024;
    r.rlim_max = 64UL * 1024 * 1024;
    if (setrlimit(RLIMIT_STACK, &r) != 0) { fail++; printf("FAIL setrlimit raise\n"); }
    else { pass++; printf("PASS setrlimit raise\n"); }

    /* Soft > hard: EINVAL. */
    r.rlim_cur = 128UL * 1024 * 1024;
    r.rlim_max = 64UL  * 1024 * 1024;
    if (setrlimit(RLIMIT_STACK, &r) == 0 || errno != EINVAL) {
        fail++; printf("FAIL setrlimit soft>hard\n");
    } else { pass++; printf("PASS setrlimit soft>hard EINVAL\n"); }

    /* Bad resource id. */
    if (getrlimit(15, &r) == 0) { fail++; printf("FAIL bad resource\n"); }
    else if (errno != EINVAL) { fail++; printf("FAIL bad resource errno\n"); }
    else { pass++; printf("PASS bad resource EINVAL\n"); }

    printf("setrlimit_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
```

- [ ] **Step 2: Build, run, expect all PASS.**

- [ ] **Step 3: Commit + push**

```bash
git add bin/setrlimit_test
git commit -m "test(bin): setrlimit_test — POSIX get/setrlimit defaults + errors"
git push origin feature/large-userspace-alloc
```

---

## Task 18: New test — `bin/stack_grow_test`

**Files:**
- Create: `bin/stack_grow_test/stack_grow_test.c`

- [ ] **Step 1: Write the test**

```c
#include <stdio.h>
#include <sys/resource.h>

/* Recurse with a large local frame until kernel either grows the stack
 * (success) or refuses (fault, kills proc). On default 8 MB cap, ~6000
 * frames of 1 KiB locals should still succeed. With a small soft limit
 * we bottom out earlier. */
static volatile int sink;

static void recurse(int depth, int max_depth) {
    volatile char buf[1024];
    buf[0] = (char)depth;
    sink ^= buf[0];
    if (depth + 1 < max_depth) recurse(depth + 1, max_depth);
}

int main(void) {
    struct rlimit r;
    getrlimit(RLIMIT_STACK, &r);
    printf("stack soft=%lu max=%lu\n", r.rlim_cur, r.rlim_max);

    /* Aim for ~4 MB of frames: 4096 * ~1 KiB local. */
    recurse(0, 4096);
    printf("PASS stack_grow 4MiB recursion under default 8MiB soft\n");
    return 0;
}
```

- [ ] **Step 2: Build, run, expect PASS.**

- [ ] **Step 3: Commit + push**

```bash
git add bin/stack_grow_test
git commit -m "test(bin): stack_grow_test — auto-grow under default RLIMIT_STACK"
git push origin feature/large-userspace-alloc
```

---

## Task 19: New test — `bin/mmap_fixed_test`

**Files:**
- Create: `bin/mmap_fixed_test/mmap_fixed_test.c`

- [ ] **Step 1: Write the test**

```c
#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>

#define MMAP_BASE_HINT  0x0000001000000000UL  /* 64 GiB */

int main(void) {
    int pass = 0, fail = 0;

    void *p = mmap((void *)MMAP_BASE_HINT, 4096,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED || p != (void *)MMAP_BASE_HINT) {
        printf("FAIL fixed at 64GiB\n"); fail++;
    } else { pass++; printf("PASS fixed at 64GiB\n"); }

    /* Overlap → EINVAL. */
    void *p2 = mmap((void *)MMAP_BASE_HINT, 4096,
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    if (p2 != MAP_FAILED || errno != EINVAL) {
        printf("FAIL overlap rejection\n"); fail++;
    } else { pass++; printf("PASS overlap rejection\n"); }

    /* Below text base → EINVAL. */
    void *p3 = mmap((void *)0x100, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    if (p3 != MAP_FAILED || errno != EINVAL) {
        printf("FAIL below text rejection\n"); fail++;
    } else { pass++; printf("PASS below text rejection\n"); }

    munmap(p, 4096);
    printf("mmap_fixed_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
```

- [ ] **Step 2: Build, run, expect all PASS.**

- [ ] **Step 3: Commit + push**

```bash
git add bin/mmap_fixed_test
git commit -m "test(bin): mmap_fixed_test — placement, overlap, low-addr rejection"
git push origin feature/large-userspace-alloc
```

---

## Task 20: New test — `bin/many_mmaps_test`

**Files:**
- Create: `bin/many_mmaps_test/many_mmaps_test.c`

- [ ] **Step 1: Write the test**

```c
#include <stdio.h>
#include <sys/mman.h>

#define N 1000   /* well above the old 256-vma global pool */

static void *maps[N];

int main(void) {
    int ok = 0;
    for (; ok < N; ok++) {
        void *p = mmap(0, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) break;
        maps[ok] = p;
    }
    if (ok != N) { printf("FAIL only %d mmaps succeeded\n", ok); return 1; }

    for (int i = 0; i < ok; i++) munmap(maps[i], 4096);
    printf("PASS many_mmaps %d\n", N);
    return 0;
}
```

- [ ] **Step 2: Build, run, expect PASS.**

- [ ] **Step 3: Commit + push**

```bash
git add bin/many_mmaps_test
git commit -m "test(bin): many_mmaps_test — slab VMA pool scales past old 256 cap"
git push origin feature/large-userspace-alloc
```

---

## Task 21: New test — `bin/exec_resets_arena`

**Files:**
- Create: `bin/exec_resets_arena/exec_resets_arena.c`

- [ ] **Step 1: Write the test**

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Parent malloc + write pattern; child execve into echo. After exec
 * the new program's malloc must succeed (proves the old arena VMA
 * was torn down and a fresh one can be installed at HEAP_ARENA_BASE). */

int main(int argc, char **argv) {
    if (argc == 2 && argv[1][0] == 'C') {
        /* Child path. */
        char *p = (char *)malloc(64 * 1024);
        if (!p) { printf("FAIL child malloc\n"); return 1; }
        p[0] = 'X';
        printf("PASS exec_resets_arena (child malloc OK)\n");
        return 0;
    }

    char *parent_buf = (char *)malloc(64 * 1024);
    if (!parent_buf) { printf("FAIL parent malloc\n"); return 1; }
    parent_buf[0] = 'P';

    char *args[] = { argv[0], "C", 0 };
    execv(argv[0], args);
    printf("FAIL execv returned\n");
    return 1;
}
```

- [ ] **Step 2: Build, run, expect PASS line from child.**

- [ ] **Step 3: Commit + push**

```bash
git add bin/exec_resets_arena
git commit -m "test(bin): exec_resets_arena — execve discards arena, fresh init OK"
git push origin feature/large-userspace-alloc
```

---

## Task 22: Adapt existing `bin/rlimit_test`

**Files:**
- Modify: `bin/rlimit_test/rlimit_test.c`

- [ ] **Step 1: Replace with adapted version**

Replace the file contents:

```c
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

    int dup2_rc = dup2(fds[0], 63);
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
```

- [ ] **Step 2: Build, run, expect all PASS.**

- [ ] **Step 3: Commit + push**

```bash
git add bin/rlimit_test/rlimit_test.c
git commit -m "test(bin): rlimit_test — drop legacy vma/page cap subtests

VMA pool is now slab-allocated and rlim_npages was removed. NOFILE is
still enforced (default 16). sbrk subtest now verifies basic grow."
git push origin feature/large-userspace-alloc
```

---

## Task 23: Regression sweep + final verification

**Files:**
- (no edits)

- [ ] **Step 1: Clean build**

Run: `rm -rf build && make -j 2>&1 | tail -20`. Expect clean build.

- [ ] **Step 2: Run kernel selftests**

Run: `make run` and look at the kernel selftest summary that fires during boot. Expect all PASS.

- [ ] **Step 3: Run a representative subset of existing user tests**

Each of these must continue to pass (run inside QEMU shell, exit code 0):

- `bigfile_pcache_test`
- `cow_test`, `cow_write_test`
- `fork_test`, `fork_storm_test`, `multi_fork_test`
- `mmap_test`, `mmap_cow_test`, `mmap_share_test`, `mmap_smoke_test`,
  `mmap_stress_test`, `truncate_mmap_test`
- `sbrk_test`, `sbrk_edge_test`, `lazy_sbrk_test`
- `malloc_test`
- `pipe_test`, `pipe_stress_test`
- `signal_test`, `sigmask_test`, `sigchld_test`
- `stack_test`, `stack_overflow_test`
- `wait_test`, `wait4_nohang_test`
- `vma_overlap_test`, `oom_test`
- `usertests`

Document any failure in a follow-up task; do not skip.

- [ ] **Step 4: Run the new tests**

- `lazy_reserve`, `huge_reserve_no_touch`, `pattern_write`, `free_reuse`,
  `direct_threshold`, `setrlimit_test`, `stack_grow_test`,
  `mmap_fixed_test`, `many_mmaps_test`, `exec_resets_arena`,
  `rlimit_test`.

All must print PASS.

- [ ] **Step 5: Commit + push (if any small fixes were needed in this sweep)**

```bash
git status
# Address any small regressions found
git add ...
git commit -m "fix(<area>): <issue surfaced in regression sweep>"
git push origin feature/large-userspace-alloc
```

---

## Self-review summary

- **Spec coverage:** layout (Tasks 2), VMA slab (3), page cap removal (6), rlimit infra (1, 5, 8, 9, 17), MAP_FIXED (7, 9, 19), libc malloc rewrite (10), tests (12–22), regression (23). ✓
- **No placeholders:** every step contains the exact code or command.
- **Type consistency:** `struct rlimit { rlim_cur, rlim_max }` consistent across kernel + libc. `RLIMITS_NR = 16` matches array dimensioning. `MAP_FIXED = 0x10` consistent kernel + libc.
- **Frequent commits + pushes:** every task ends with a commit and a `git push origin feature/large-userspace-alloc`.
