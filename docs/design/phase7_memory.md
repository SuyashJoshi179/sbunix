# Phase 7 — VMAs, sbrk, malloc, mmap-anon, COW fork

**Status:** design ready, not yet implemented
**Depends on:** Phase 3 (fork, exec, pagetables), Phase 4 (file-backed inodes), Phase 5 (real files for file-backed mmap in a follow-up), Phase 6 (shell as a memory-heavy workload)
**Enables:** Phase 7.5 (MicroPython demands a real heap), Phase 8 (signal stack mapping), Phase 9 (unified cleanup, leak tests)

---

## 1. Goal

Turn the process address space from "a linear list of pages copied at fork" into a **list of VMAs** (virtual memory areas) with proper semantics: growable heap via `sbrk`, anonymous memory via `mmap(MAP_ANON)`, user-space `malloc` on top of `sbrk`, and **copy-on-write fork** so that spawning a process that is about to `exec` does not copy megabytes of pages needlessly.

This is the biggest single-phase change since Phase 3. Split into four sub-PRs (7a–7d) so each one is independently testable. At the end, `fork()` is cheap, `malloc()` works, and MicroPython can run.

---

## 2. Preconditions

- SV39 page tables with `uvmmap`, `uvmunmap`, `uvmcopy` (Phase 3 deep-copy version) already in place.
- Page allocator (`page_alloc`, `page_free`) returning zeroed pages.
- Trap handler routes page faults (scause 12/13/15) through `user_fault()` which currently just kills the process (Phase 3 tail).
- `fork`, `exec`, `wait`, file-backed `open`/`read` all work.
- Phase 5's sbfs is writable so we have somewhere to back file-backed mmap if we enable it this phase (we will not — see §9).

---

## 3. Concepts & data structures

### 3.1 VMA (`struct vma`)

A VMA describes a contiguous range of virtual addresses in the user address space with uniform permissions and backing.

```c
#define VMA_PROT_R   0x1
#define VMA_PROT_W   0x2
#define VMA_PROT_X   0x4
#define VMA_TYPE_ANON 1     // zero-filled on first touch
#define VMA_TYPE_FILE 2     // backed by an inode (deferred to a later phase)
#define VMA_TYPE_STACK 3    // anon but grows downward (special fault handling)
#define VMA_TYPE_HEAP 4     // anon but grows upward via sbrk

#define VMA_FLAG_COW  0x1   // writes trap, copy page, clear COW

struct vma {
    uint64_t      start;    // inclusive, page-aligned
    uint64_t      end;      // exclusive, page-aligned
    uint32_t      prot;
    uint32_t      type;
    uint32_t      flags;
    struct inode *file;     // NULL unless TYPE_FILE
    uint64_t      file_off; // NULL unless TYPE_FILE
    struct vma   *next;
};
```

The PCB gains `struct vma *vma_list;` — a sorted singly-linked list by `start`. Sorted list is fine for a process with at most ~20 VMAs (heap, stack, text, rodata, data, a handful of mmaps). Red-black trees are Phase 10 territory.

Invariants:
- VMAs never overlap.
- VMAs are page-aligned.
- The list is sorted by `start`.
- Every mapped page in the user pagetable has exactly one VMA covering it (after faults resolve).

### 3.2 Demand paging

A VMA can exist in the list with no page-table entries yet. On a user page fault:

1. Fault handler calls `vma_find(faulting_va)`.
2. If no VMA → SIGSEGV (Phase 8) / kill with SIGSEGV stub now.
3. If VMA prot doesn't permit the access → SIGSEGV.
4. Otherwise:
   - `VMA_TYPE_ANON`: `page_alloc`, zero it, map with VMA prot.
   - `VMA_TYPE_STACK`: same as anon, but extend the VMA downward if the fault is one page below current `vma->start` and we are not about to collide with the VMA below.
   - `VMA_TYPE_HEAP`: same as anon, bounded by `p->brk`.
   - `VMA_TYPE_FILE`: deferred (§9).
   - If `flags & VMA_FLAG_COW` and the fault is a write: COW copy flow (§3.4).

### 3.3 `sbrk` and `brk`

Every process has a single heap VMA `[heap_start, heap_start + 0)` created at `exec` time. `sbrk(intptr_t incr)` grows or shrinks this VMA's `end`:

```c
uintptr_t sys_sbrk(intptr_t incr) {
    uint64_t old = p->heap_vma->end;
    uint64_t new = old + incr;
    if (incr > 0) {
        if (new > HEAP_MAX) return -ENOMEM;
        p->heap_vma->end = new;    // pages mapped lazily on fault
    } else if (incr < 0) {
        if (new < p->heap_vma->start) return -EINVAL;
        uvmunmap_range(p->pgtable, new, old);  // actually free pages
        p->heap_vma->end = new;
    }
    return old;
}
```

`sbrk` returns the **old** end, matching Linux so existing malloc implementations drop in. Memory is not actually allocated — only the VMA end moves; pages come in via fault.

### 3.4 Copy-on-write

On `fork`:
1. Walk parent VMAs, clone the list (structs only, no pages).
2. For each VMA that is writable, mark both parent and child VMAs as COW (`flags |= VMA_FLAG_COW`), strip the W bit from both pagetables for pages currently mapped, and bump each page's refcount.
3. Do *not* deep-copy pages. `uvmcopy` becomes `uvmcow_share`.

On first write to a COW page:
1. Fault handler sees scause=15 (store fault), VMA is writable but PTE is not → COW fault.
2. If page's refcount is 1: just set the W bit, flush TLB, return. Cheap win.
3. Else: allocate a new page, memcpy the old contents, map it with W, decrement old page's refcount.
4. If parent's page refcount hits 1 while child still COWs → lazy: parent keeps PTE unwritable until its own COW fault, which then takes the "refcount 1" fast path. No cross-process poke-back needed.

Result: `fork` + immediate `exec` copies one stack page (maybe zero if we also COW the stack) and returns.

### 3.5 Page refcount table

To support COW we need to know how many VMAs reference a physical page. A global array indexed by PFN:

```c
static uint8_t page_refs[NPAGES];   // one byte per page, 256 max refs

static inline void page_get(void *pa) { page_refs[PA2PFN(pa)]++; }
static inline void page_put(void *pa) {
    if (--page_refs[PA2PFN(pa)] == 0) page_free(pa);
}
```

`page_alloc` now returns a page with refcount=1 (auto-initialized). `page_free` asserts the refcount was 1 — anything higher is a bug. Calling `page_put` on a refcount-1 page free's it. NPAGES is computed from the physical memory size at boot.

Upper bound 256 is fine for a single-hart toy with NPROC≤16; a process forking itself 256 deep is pathological. Upgrade to u16 if Phase 10 pushes it.

### 3.6 `mmap` (anonymous only, for now)

```c
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t off);
```

Phase 7 supports only:
- `flags = MAP_ANON | MAP_PRIVATE`.
- `fd == -1`, `off == 0`.
- `addr` ignored (kernel picks).
- `prot` is `PROT_READ | PROT_WRITE` (others valid but rare).

It allocates a new VMA in the "mmap area" range `[MMAP_START, MMAP_END)`, first-fit. Pages come in via fault. `munmap` removes the VMA and `uvmunmap`s any backing pages.

File-backed mmap, `MAP_SHARED`, `MAP_FIXED`, etc. are Phase 8+ territory (§9).

### 3.7 User `malloc`

A textbook `dlmalloc`-lite on top of `sbrk`. Free list of variable-size chunks, best-fit, coalesce on `free`. No mmap fallback for big allocs (fall back to `-ENOMEM` > 128 KiB). Ship alongside libc in `user/libc/malloc.c`. This is not research-grade; it exists so MicroPython runs.

---

## 4. File-by-file changes

### 4a — VMA refactor (no new user features)

The purpose of 7a is to replace "deep-copy pagetable in `uvmcopy`" with "VMA list + on-demand pages" *without* changing any user-visible behaviour. After 7a, every test from Phases 3–6 still passes. User-visible API surface is unchanged.

New:
- `kernel/vma.c`, `kernel/include/vma.h`
  - `vma_alloc`, `vma_free` (kmalloc/kfree wrappers with zero init).
  - `vma_find(list, va)`, `vma_insert(list, vma)`, `vma_remove(list, vma)`.
  - `vma_list_free(p)` called on process destroy.

Modified:
- `kernel/include/proc.h` — add `struct vma *vma_list`, `struct vma *heap_vma`, `uint64_t brk_start` to PCB.
- `kernel/exec.c` — when loading ELF segments, create a VMA per segment instead of calling `uvmmap` directly. Allocate pages *in the fault handler* on first touch. Create the heap VMA (zero-length) immediately after the data segment. Create a stack VMA below `USER_STACK_TOP` covering exactly one page initially (more arrive via stack fault).
- `kernel/trap.c` — route user page faults to `user_fault(scause, stval)` which consults the VMA list.
- `kernel/proc.c` — `proc_fork_current` becomes "clone VMA list + COW share pages" (this is the transition into 7c, but the scaffolding starts here).
- `kernel/uvmcopy.c` — deprecated/deleted.

**Breaking point:** every user test must still pass at end of 7a. If anything crashes because a VMA wasn't populated eagerly, the fault handler is wrong, not the test.

### 4b — `sbrk` + user malloc

New:
- `kernel/sys_mem.c` (or extension of existing syscall.c) — `sys_sbrk`.
- `user/libc/malloc.c`, `user/libc/stdlib.h` — `malloc`, `free`, `calloc`, `realloc`. ~300 LoC of hand-rolled free-list allocator.

Modified:
- `bin/*/Makefile` — link in the new libc so existing tests pick up `malloc`. Minimal risk: no existing test uses `malloc` yet (they all use fixed buffers).

### 4c — COW fork

New:
- `kernel/page_refs.c` — global refcount array, `page_get`/`page_put`.

Modified:
- `kernel/vma.c` — `vma_list_cow_share(parent, child)` walks the parent list, clones each VMA, bumps refcounts on every currently-mapped page, strips W from both sides, marks both VMAs COW.
- `kernel/trap.c` / `user_fault` — handle `scause==15` on a COW-marked VMA: allocate, copy, remap.
- `kernel/proc.c` — `free_proc` walks `vma_list` and `page_put`s every backing page before freeing the pagetable.
- `kernel/pmem.c` / `page_alloc` — initializes refcount=1; `page_free` asserts refcount==1 (catches double-free and COW accounting bugs).

### 4d — `mmap` + `munmap` + stack auto-grow

New:
- `kernel/sys_mmap.c` — `sys_mmap`, `sys_munmap`.
- `user/libc/sys/mman.h`, wrappers.

Modified:
- `user_fault` — stack VMA fault handling: extend downward if the faulting va is within one page below `stack_vma->start` and the next VMA below has ≥1 page of gap.

---

## 5. Key flows

### 5.1 `exec` with VMAs

1. Parse ELF headers.
2. For each `PT_LOAD` segment:
   - Compute `[vaddr_aligned, vaddr_aligned + memsz_aligned)`.
   - `vma_alloc`, set prot from flags, type = ANON (TYPE_FILE would let us skip the file copy, but Phase 7 leaves text segments anon — read into pages at exec time).
   - For each page: `page_alloc`, copy bytes from the file image, map into pagetable with the segment's prot.
3. Create heap VMA `[vaddr_end, vaddr_end)` aligned up to page.
4. Create stack VMA `[USER_STACK_TOP - PGSIZE, USER_STACK_TOP)`; allocate one page, copy argv/envp onto it.
5. Set `user_sp`, `user_entry`, switch to user mode.

Later in this phase we pivot from "read pages at exec time" to "demand page from file" but that is §9.

### 5.2 Page fault → zero page

1. Hardware store-page-fault, `stval=0x123000`.
2. `user_fault` finds VMA at `0x123000` with type=ANON, prot=R|W.
3. `page_alloc` → `p`, zeroed.
4. `uvmmap(p->pgtable, 0x123000, p, PTE_R|PTE_W|PTE_U)`.
5. `sret` → user instruction retries, succeeds.

### 5.3 Page fault → COW copy

1. Store fault, PTE present with R but not W, VMA prot has W, VMA COW flag set.
2. `uint8_t *pa = PTE_TO_PA(pte);`
3. If `page_refs[PFN(pa)] == 1`:
   - Just `pte |= PTE_W`, flush TLB.
4. Else:
   - `new = page_alloc();`
   - `memmove(new, pa, PGSIZE);`
   - Replace PTE with `(new, prot|W)`.
   - `page_put(pa);`

Critical detail: the decision must be made while holding whatever lock covers `page_refs`. On single hart this is IRQs-off. If we ever go SMP, a per-page spinlock or atomic CAS is needed.

### 5.4 Stack growth

Process's stack VMA currently starts at `0x3fff_f000`. Function call spills `sp` to `0x3fff_e100`.

1. Fault at `stval=0x3fff_e100`.
2. `vma_find` returns NULL. Naively we kill the process.
3. **Stack extension hook:** before killing, check "is `stval` one page below an existing STACK-type VMA, and is there gap?"
   - Yes → extend `stack_vma->start` to `0x3fff_e000`, demand-allocate that page, return.
   - No → SIGSEGV.

Bound the growth: `MAX_STACK = 1 MiB`. Attempts beyond that get SIGSEGV, not more pages.

### 5.5 `munmap` of a partial VMA

`munmap(start, len)` where `[start, start+len)` is a strict subset of an existing VMA splits it in three: keep-left, unmapped-middle, keep-right. Implementation:

- Find covering VMA.
- Allocate a new VMA for the right remainder (copy of the old one with adjusted start).
- Shrink the original VMA's end to `start`.
- Walk pages in `[start, start+len)` and `uvmunmap` them (decrementing refcounts).

Edge: `munmap` exactly at a VMA boundary degenerates to one of the simpler cases. Test all of them (§7).

---

## 6. Syscall ABI & error codes

| Syscall  | Num | Args                                                | Returns                                  |
|----------|-----|-----------------------------------------------------|------------------------------------------|
| `sbrk`   | 70  | `intptr_t incr`                                     | old brk, `-ENOMEM`, `-EINVAL`            |
| `mmap`   | 71  | `void *addr, size_t len, int prot, int flags, int fd, off_t off` | addr, `-ENOMEM`, `-EINVAL`, `-EACCES` |
| `munmap` | 72  | `void *addr, size_t len`                            | 0, `-EINVAL`, `-ENOMEM` (on split-fail)  |

Existing syscalls pick up:
- `fork` — now returns `-ENOMEM` on COW setup failure (unlikely but possible).
- `exec` — returns `-ENOMEM` if segment VMA allocation fails.

---

## 7. Test plan

### 7.1 Unit tests (`kernel/selftest.c`)

| ID  | Test                                                                 | What it proves |
|-----|----------------------------------------------------------------------|----------------|
| U-1 | Alloc page, check refcount=1; `page_get`, check 2; `page_put` twice  | Refcount accounting |
| U-2 | `vma_insert` 10 non-overlapping VMAs, `vma_find` each                | Sorted insert, correct lookup |
| U-3 | `vma_insert` overlap → panic or assert                               | Overlap detection |
| U-4 | `vma_list_cow_share` a list of 5 VMAs                                | Child list mirrors, refcounts bumped |
| U-5 | Simulate fault on anon VMA                                           | Allocates, zero-fills, maps |
| U-6 | Simulate fault on non-VMA address                                    | Returns SIGSEGV/kill |
| U-7 | Simulate COW fault with refcount=1                                   | Takes fast path (no new page) |
| U-8 | Simulate COW fault with refcount=2                                   | Allocates new, decrements old |
| U-9 | `vma_split` in the middle, left, right of a VMA                      | All three cases produce correct lists |

### 7.2 End-to-end tests (`bin/`)

| ID  | Binary                 | Scenario |
|-----|------------------------|----------|
| E-1 | `sbrk_test`            | `sbrk(0)` returns heap start; `sbrk(4096)` returns old; touches new page without fault |
| E-2 | `sbrk_shrink_test`     | `sbrk(4096)`, touch, `sbrk(-4096)`, re-touch → SIGSEGV |
| E-3 | `malloc_test`          | Allocate 100 blocks of varying size, free in random order, re-allocate, check contents |
| E-4 | `mmap_anon_test`       | `mmap(0, 8192, RW, ANON|PRIVATE, -1, 0)`, write, read back, `munmap`, touch → SIGSEGV |
| E-5 | `mmap_split_test`      | `mmap` 3 pages, `munmap` the middle, access first and third still works, middle faults |
| E-6 | `fork_cow_test`        | Fork, write in child to a shared page, parent's copy unchanged |
| E-7 | `fork_cow_refcount`    | Fork, child exits immediately, parent's PTEs regain W lazily on first write |
| E-8 | `stack_grow_test`      | Deep recursion pushes stack past initial single page, no crash |
| E-9 | `exec_heap_reset_test` | After `exec`, `sbrk(0)` reflects the new program's brk (not the old one) |

### 7.3 Limit / stress tests

| ID   | Binary                     | Boundary                                                                                   |
|------|----------------------------|--------------------------------------------------------------------------------------------|
| L-1  | `malloc_fill_test`         | Allocate until `-ENOMEM`, free half, allocate again → correct coalescing                   |
| L-2  | `malloc_fragment_test`     | Interleave 64 small and 64 large allocs, free smalls, allocate one large — coalesce works  |
| L-3  | `sbrk_max_test`            | `sbrk(+HEAP_MAX)`, touch every page, `sbrk(-HEAP_MAX)`                                     |
| L-4  | `mmap_many_test`           | 32 separate mmaps, munmap all → no VMA leak, no page leak (check via `/proc` or selftest)  |
| L-5  | `fork_storm_test`          | Fork 16 children, each writes its own page, all exit, parent checks heap intact            |
| L-6  | `cow_refcount_saturation`  | Fork depth so that a single page has refcount=8; every child COWs, refcounts decrement back|
| L-7  | `stack_overflow_test`      | Recurse until stack hits MAX_STACK, confirm SIGSEGV at the right boundary                  |
| L-8  | `mmap_fault_storm`         | Mmap 1024 pages, touch each one linearly — measure that every fault is O(log N) or faster (lookup time) |
| L-9  | `cow_fork_exec`            | Benchmark: 1000 iterations of `fork()+exec()`. Should be ≥10× faster than deep-copy fork from Phase 3. Prove COW actually wins. |
| L-10 | `page_leak_audit`          | Selftest: run E-1..E-9 in sequence, at end assert `free_pages_at_start == free_pages_at_end` |
| L-11 | `cow_write_both_sides`     | Both parent and child write to the same COW page in a tight loop, both see their own values |
| L-12 | `munmap_double_free_test`  | `munmap` same range twice — second returns `-EINVAL`, no panic, no page freed twice         |
| L-13 | `sbrk_then_mmap_collision` | `sbrk` up to `HEAP_MAX`, then `mmap` at MMAP_START — no overlap, no corruption              |

L-10 (page leak audit) is the most important test in this phase. A tiny leak on every fork will crash the system after 10 minutes of shell use. Make the audit run **after every single test** during development, then gate it to once-at-end for CI speed.

### 7.4 Test gate

- All Phase 3–6 tests still pass, **unchanged**. Any test that now fails is a VMA regression.
- L-9 shows a measurable COW speedup. Write the number into the commit message.
- L-10 shows zero page leaks across the entire test suite.
- MicroPython (Phase 7.5) builds and runs "hello world" against the Phase 7 libc.

---

## 8. Gotchas

- **`page_free` must assert refcount==1.** Every time you forget `page_put` where you needed it, you get a page freed with refcount=2 and the assert catches it. Without the assert you get silent use-after-free months later.
- **TLB flushes on COW.** After you swap in the new page you **must** `sfence.vma` the VA (or flush everything). Old PTE is in the TLB. Skipping this produces the world's most confusing "wrote to page but parent still sees old value" bug.
- **Stack growth must check the gap.** If you blindly extend the stack VMA downward into an existing `mmap` VMA you will merge two unrelated regions and corrupt the list. Always verify there is at least one free page between `stack_vma->start - PGSIZE` and the next lower VMA's `end`.
- **`sbrk` negative grow must free pages now.** Otherwise `malloc` can `sbrk(+N)`, `free`, `sbrk(-N)` and leak. Walk the range and `uvmunmap`/`page_put` every mapped page.
- **`fork` COW and shared file fds.** The VMA list clones separately from the fd table. Both must be in lockstep. A partial failure halfway through fork must unwind both. Write the unwind code and test it (kmalloc failure injection if necessary).
- **COW over the stack page.** If the stack starts life writable and fork marks it COW, the first function call in the child triggers a COW fault on the stack. Make sure the COW fault handler runs on a kernel stack, not the user stack it's trying to modify. Already true because user traps switch to ksp via sscratch, but audit once.
- **`vma_find` linear search is O(N).** Fine for N≤20. Phase 10 will push it higher; do not optimize now, but leave a comment.
- **`kmalloc` for `struct vma` must not itself take a page fault on a user VMA.** Trivial: vma structs live in kernel heap. Just don't accidentally stash them in user-addressable regions.
- **`mmap`'s returned address must land in the reserved mmap window.** If you let it overlap heap, `sbrk` can one day crash into it. Fixed window from `MMAP_START` to `MMAP_END`, disjoint from heap and stack, no exceptions.
- **Don't forget `munmap` on process exit.** `free_proc` walks the VMA list; for every VMA, walk pages and `page_put`. If you rely on `free_user_pgtable` to reclaim everything you will miss refcounts and leak.
- **Page refcount byte overflow.** `u8` caps at 255. Fork depth beyond that panics. Document as a known limit. Upgrade to u16 when Phase 10 actually hits it.
- **User malloc bugs are not kernel bugs.** Keep `user/libc/malloc.c` tests separate from kernel selftests. Debug it with `malloc_test` first; do not let malloc bugs contaminate your kernel memory tests.

---

## 9. Open questions / decisions punted

1. **Demand-paged text segments (file-backed read-only mmap).** Would cut `exec` cost and memory use. Adds `TYPE_FILE` VMA handling and a read-through path to the inode. **Lean:** not in Phase 7. Phase 7 keeps text segments anon (read at exec time). Revisit in Phase 8 or when it bites.
2. **`MAP_SHARED`.** Requires a reverse map from pages to VMAs for writeback. Large lift. **Lean:** never, or Phase 10. `MAP_PRIVATE | MAP_ANON` covers every use case up to BusyBox.
3. **`mprotect`.** Easy to add but not needed by anything yet. Skip.
4. **`mlock`, `madvise`.** Skip, stub as `0` if anything asks.
5. **Lazy COW on stack.** Should fork COW the single stack page or deep-copy it? Both work; COW is simpler. **Lean:** COW everything uniformly.
6. **`brk` vs `sbrk` interface.** Linux has both. We ship `sbrk` only; `brk` is a trivial wrapper in libc if needed.
7. **Process-level memory accounting.** Do we track RSS in the PCB? Nice for `top` one day. **Lean:** add a `uint64_t rss_pages` field, increment on fault, decrement on unmap. Essentially free.
8. **Multiple heap regions.** `malloc` assumes one contiguous heap. Fine for now. Phase 10 `dlmalloc` upgrade can switch to mmap-based big allocs.

---

## 10. Exit criteria

- [ ] VMA list replaces "deep-copy pagetable on fork". Phase 3–6 tests unchanged.
- [ ] `sbrk`, `mmap(MAP_ANON|MAP_PRIVATE)`, `munmap` work; user `malloc`/`free` round-trips random workloads.
- [ ] COW fork: parent and child share pages until either writes, then each has its own.
- [ ] Stack grows on demand up to `MAX_STACK`, then SIGSEGV.
- [ ] `fork`+`exec` benchmark (L-9) shows measurable speedup vs Phase 3 deep-copy.
- [ ] Page leak audit (L-10) reports zero leaks across the full test suite.
- [ ] MicroPython (Phase 7.5) can be built and its memory init does not crash.
- [ ] `kernel/selftest.c` total assertions increase by ≥60.
- [ ] `docs/design/README.md` updated to Phase 7 shipped.
