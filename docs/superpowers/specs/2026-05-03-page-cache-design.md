# Page Cache for Files — Design Spec

**Date:** 2026-05-03
**Status:** Approved for implementation (Phase B). Phase C (write-back) and Phase D (adaptive sizing, read-ahead) captured as follow-ups.

## 1. Problem

Today in SBUnix:

- `read()` on regular files goes through the **bio block cache** (`kernel/bio.c`): 32 slots × 512 B = 16 KB total, indexed by `blockno`.
- `mmap()` (`kernel/syscall.c::sys_mmap`) only supports `MAP_ANON` — explicitly rejects `fd != -1`. There is no way to map a regular file.
- There is no page-indexed file cache. Even if `mmap` of regular files were enabled today, it could not share pages with `read()` because the two paths use different caches with different indexing (block# vs. (inode, page#)).

Concrete gaps this causes:

1. `mmap` of `/bin/sh` (or any binary in tarfs) is impossible. Loaders must `read` into private buffers.
2. If two processes both `read` the same file, each materialises private copies in user space. Page-level sharing is not available.
3. `mmap`-write semantics (visible to subsequent `read` via fd) cannot be implemented at all.

## 2. Goal

Add a unified, page-indexed file cache at the **VFS layer**, so that:

- `read()`, `write()`, and `mmap()` of regular files all funnel through the same `(inode, page_index) → 4 KB page` table.
- All filesystems that expose regular files (sbfs, tarfs) participate via two new inode-ops hooks: `readpage` and `writepage`.
- `MAP_PRIVATE` (read-only and copy-on-write) and `MAP_SHARED` (including writable) are correctly supported. Writes via `MAP_SHARED` are visible through `read()` and vice versa.
- Bio remains authoritative for sbfs disk blocks. Page cache is **write-through** to bio in this phase (Phase B). Phase C will later convert to write-back.

## 3. Non-Goals (Phase B)

- Write-back / lazy flush. Out of scope, deferred to Phase C — see §10.
- Adaptive cache sizing or read-ahead. Phase D.
- Caching for synthetic filesystems (devfs, procfs). Their `read` ops remain unchanged; without `readpage`, they bypass the cache.

## 4. Architecture Overview

```
                 read()/write()                       mmap()
                       │                                │
                       ▼                                ▼
            generic_file_read/write          generic_file_mmap (vma fault)
                       │                                │
                       └──────────► page_cache ◄────────┘
                                          │
                            ops->readpage/writepage
                                          │
                  ┌───────────────────────┼───────────────────────┐
                  ▼                       ▼                       ▼
              sbfs_readpage         tarfs_readpage          (devfs/procfs:
              sbfs_writepage           (RO)                  no readpage,
                  │                                          fall back to
                  ▼                                          ops->read)
               bio (bread/bwrite,
               crash-safe log)
```

## 5. Components

### 5.1 Page-cache core — `kernel/include/page_cache.h`, `kernel/page_cache.c`

Fixed pool, LRU. **64 slots × 4 KB = 256 KB total.** Constant `PCACHE_NSLOTS = 64`, defined in the header (analogous to `NBUF` in `bio.h`). Phase D may grow this dynamically.

```c
struct pcache_page {
    struct inode  *ip;          /* NULL = free slot */
    uint64_t       pgidx;       /* page index within file */
    void          *page;        /* 4 KB physical page (kalloc'd at init) */
    int            refcnt;      /* held by readers/writers/mmap mappings */
    int            dirty;       /* set by MAP_SHARED write fault */
    int            valid;       /* readpage succeeded */
    struct pcache_page *prev, *next;   /* LRU doubly-linked list */
};

void pcache_init(void);

/* Lookup or allocate. On miss, calls ip->ops->readpage to fill the page.
 * Returns with refcnt >= 1 on success. */
int  pcache_get(struct inode *ip, uint64_t pgidx, struct pcache_page **out);

/* Drop one refcnt. When refcnt hits 0, page goes to LRU tail (eligible for
 * eviction). Pages with refcnt > 0 are pinned. */
void pcache_put(struct pcache_page *p);

/* Force-flush and drop all pages of an inode. Called on:
 *   - last close of an inode whose pages might be dirty,
 *   - inode_put when refcnt hits 0,
 *   - filesystem teardown.
 * Dirty pages are written back via ops->writepage (caller wraps begin_op
 * for sbfs). Pages with refcnt > 0 remain (pinned by mmap); the function
 * returns -EBUSY in that case after flushing what it can. */
int  pcache_flush_inode(struct inode *ip);

/* Drop pages in [off, off+len). Used by truncate. Mapped pages stay
 * (caller must rely on VMA bounds checking to surface SIGBUS). */
void pcache_invalidate_range(struct inode *ip, uint64_t off, uint64_t len);
```

Eviction policy: walk LRU from oldest, skip `refcnt > 0`, on first dirty hit call `writepage` (Phase B: dirty only on `MAP_SHARED`-write pages, rare; wrap in begin_op for sbfs), then reuse the slot. If all slots pinned → return `-ENOMEM`.

### 5.2 inode_ops additions — `kernel/include/inode.h`

```c
struct inode_ops {
    /* ...existing fields... */

    /* Fill `page` (always 4096 bytes) with data from this inode at byte
     * offset pgidx*4096. Reads beyond EOF zero-fill the tail. Returns 0
     * on success or -errno. NULL on filesystems that do not participate
     * in the page cache (devfs/procfs). */
    int (*readpage) (struct inode *, uint64_t pgidx, void *page);

    /* Write `page` (4096 bytes) back to inode at offset pgidx*4096.
     * Only the bytes within current file size are persisted; tail past
     * EOF must not extend the file (size updates are done by the
     * generic_file_write path, not writepage). NULL = read-only fs. */
    int (*writepage)(struct inode *, uint64_t pgidx, const void *page);
};
```

### 5.3 VFS generic helpers — `kernel/fs/vfs.c`

```c
/* Read up to `n` bytes from inode at `off` into `buf`. Loops over the
 * pages spanned by the request, pcache_get each, memcpy slice, pcache_put. */
ssize_t generic_file_read (struct inode *ip, uint64_t off,
                           void *buf, size_t n);

/* Write up to `n` bytes. For each spanned page:
 *   - pcache_get (readpage if partial-page write)
 *   - memcpy bytes in
 *   - call ip->ops->writepage immediately (write-through, Phase B)
 *   - pcache_put
 * Caller is responsible for begin_op/end_op (sbfs). Updates ip->size and
 * mtime on extending writes. Phase C will drop the immediate writepage. */
ssize_t generic_file_write(struct inode *ip, uint64_t off,
                           const void *buf, size_t n);

/* Page-fault handler entry for VMA_TYPE_FILE. Called from
 * user_page_fault. Looks up cache page, installs PTE. */
int generic_file_fault(struct vma *v, uint64_t va);
```

Filesystems with `readpage` set their `ops->read` to a thin wrapper:
`return generic_file_read(ip, off, buf, n)`. Same for `write`.

Filesystems without `readpage` (devfs/procfs) keep current bespoke `read` implementations — unchanged.

### 5.4 sbfs

`kernel/fs/sbfs.c`:

- `sbfs_readpage(ip, pgidx, page)`: locate the 8 logical blocks at byte offset `pgidx*4096` via existing block-map walker. For each block: `bread`, memcpy 512 B into `page+i*512`, `brelse`. Bytes past EOF are zeroed.
- `sbfs_writepage(ip, pgidx, page)`: 8 × `log_write` (one per 512 B block). Caller wraps in begin_op/end_op. Blocks past current file size are skipped.
- `sbfs_read` / `sbfs_write` rewritten as thin wrappers calling the generic helpers (handle their own begin_op/end_op around `generic_file_write`).

### 5.5 tarfs

`kernel/fs/tarfs.c`:

- `tarfs_readpage(ip, pgidx, page)`: memcpy 4 KB from the in-memory tar blob slice for this inode into `page`. Tail past EOF zero-filled. No I/O.
- `tarfs_writepage` = NULL (read-only).
- `tarfs_read` becomes wrapper over `generic_file_read`. Existing direct memcpy path deleted.

### 5.6 mmap path — `kernel/syscall.c`, `kernel/fs/vfs.c`, page-fault handler

`sys_mmap`:

- Drop the `fd != -1 || off != 0` rejection.
- If `!MAP_ANON`: validate `fd` is open, refers to a regular file (`I_REG`), and the inode has `readpage` set. Otherwise `-EINVAL`.
- Permission check: `MAP_SHARED && (prot & PROT_WRITE)` requires file opened with write access; `prot & PROT_READ` requires read access.
- Allocate VMA with `type = VMA_TYPE_FILE`, `file = inode_get(ip)`, `file_off = off` (validated page-aligned).
- `VMA_FLAG_SHARED` (new flag) marks `MAP_SHARED`.

User page-fault handler (`user_page_fault` in `kernel/proc.c` or wherever it lives):

- For `VMA_TYPE_FILE`:
  - `pgidx = (faulting_va - v->start) / PGSZ + v->file_off / PGSZ`
  - Bounds check against `v->file->size` rounded up; past-EOF → SIGBUS.
  - `pcache_get(v->file, pgidx, &p)` — holds refcnt across the mapping lifetime (see §5.7).
  - Install PTE pointing at `p->page`. Writability:
    - `PROT_READ` only → RO PTE.
    - `PROT_WRITE && MAP_SHARED` → install RO on read fault; on subsequent write fault, set `p->dirty = 1` and upgrade PTE to RW. This way we observe the moment a page becomes dirty without scanning RISC-V PTE dirty bits.
    - `PROT_WRITE && MAP_PRIVATE` → install RO; on write fault, take CoW path (allocate anon page, memcpy from cache page, swap PTE to point at anon copy, drop pcache refcnt for that page). Reuses existing anon CoW machinery.

### 5.7 Mapping lifecycle (refcnt invariant)

A file VMA holds **one pcache refcnt per page that it has actually faulted in** (lazy). On fork, child VMAs are duplicated but child PTEs are cleared; child re-faults on access and acquires its own refcnt. This matches today's CoW behaviour.

On VMA teardown (munmap, exit, etc.):

- For each present PTE in the VMA range that points at a pcache page:
  - If `MAP_SHARED && page->dirty`: `ip->ops->writepage` (sbfs caller wraps begin_op).
  - `pcache_put` to release refcnt.
- For CoW-private anon pages: existing anon free path.

This pcache walk is encapsulated in `vma_drop_file_pages(v)` called from the existing VMA cleanup path.

### 5.8 New syscall — `sys_msync`

```c
int msync(void *addr, size_t len, int flags);   /* syscall # 115 */
```

Walks VMAs in `[addr, addr+len)`. For each `MAP_SHARED` file VMA, flush all dirty pages whose VAs intersect the range via `writepage`, clear dirty. `flags` accepted: `MS_SYNC` (only mode supported); other values → `-EINVAL`.

### 5.9 `sys_close` / inode_put hook

When the last fd referencing an inode closes AND the inode has no live mappings, `pcache_flush_inode` is called from `inode_put` so dirty pages don't survive into eviction land without owners. With write-through this is normally a no-op.

### 5.10 Truncate

`generic_file_truncate` (or per-fs `truncate` op): on size shrink, call `pcache_invalidate_range(ip, new_size, ~0)`. Mapped pages past EOF stay pinned but the fault handler's bounds check rejects new accesses with SIGBUS.

## 6. Data Flow Summary

| Operation | Path |
|---|---|
| `read` (cached) | `generic_file_read` → `pcache_get` (hit) → memcpy out |
| `read` (miss) | `generic_file_read` → `pcache_get` (miss) → `ops->readpage` → memcpy out |
| `write` | `generic_file_write` → `pcache_get` → memcpy in → `ops->writepage` → put |
| `mmap` setup | `sys_mmap` → vma_alloc(`VMA_TYPE_FILE`) |
| `mmap` first touch | page fault → `pcache_get` → install PTE |
| `MAP_SHARED` write | write fault → set dirty + RW PTE |
| `msync` / munmap | walk pages → if dirty → `writepage` → put |
| `truncate` shrink | `pcache_invalidate_range` |

## 7. Error Handling

- `pcache_get` failure (pool exhausted, all pinned): `-ENOMEM`. `read`/`write` return short count if any progress, else `-ENOMEM`. `mmap` fault → SIGBUS.
- `readpage` I/O failure: `-EIO`. `read` short count or `-EIO`. mmap fault → SIGBUS, page marked invalid (re-fetched on next get).
- `writepage` failure (write-through write): `-EIO` from `write`. Page invalidated to force re-read from disk on next access.
- `pcache_get` cannot evict because all slots pinned and dirty: returns `-ENOMEM` (no sleep primitive in this kernel).
- Truncate vs. mmap: pages past new EOF kept while pinned; fault bounds check returns SIGBUS for new accesses.
- Fork: file VMA dup is shallow; per-page pcache refcnts acquired lazily on child fault. No O(pages) work at fork time.

## 8. Memory Sizing

- Pool: 64 × 4 KB = 256 KB. Allocated once at boot via existing pmem allocator. Never freed.
- `struct pcache_page` metadata: ~64 B × 64 = 4 KB. One static array.
- Phase D may convert to demand-grow with high-watermark eviction.

## 9. Testing

### 9.1 Kernel selftests — `kernel/selftest.c`

1. `pcache_basic_test`: two `pcache_get` of same `(ip, pgidx)` return same page, refcnt = 2.
2. `pcache_evict_test`: fill pool with `N+1` distinct `(ip, pgidx)` pairs (releasing refcnt each); LRU oldest evicted; `pcache_get` of oldest miss → readpage called again.
3. `pcache_dirty_writeback_test`: synthesise dirty page, force eviction, assert `writepage` was called and disk content matches.
4. `pcache_pin_test`: with all slots pinned, additional `pcache_get` returns `-ENOMEM`.

### 9.2 Userland tests — `bin/`

1. `pagecache_test`: write file via fd at offsets 0 and 8192; mmap PROT_READ MAP_SHARED; verify mapping bytes match. Then mmap PROT_WRITE MAP_SHARED, modify byte at offset 100, msync, read via fd → sees modification.
2. `mmap_share_test`: parent maps file MAP_SHARED PROT_READ; fork; child maps same file; both read, both see same bytes. (Sharing of physical pages verified indirectly via meminfo before/after — page count rises by file_pages, not 2*file_pages.)
3. `mmap_cow_test`: MAP_PRIVATE PROT_WRITE; child writes; parent reads original; file on disk unchanged.
4. `truncate_mmap_test`: open + mmap PROT_READ MAP_SHARED whole file; truncate to half; access second-half VA → SIGBUS (handler returns).
5. `bigfile_pcache_test`: write 1 MB file (well over 256 KB pool), sequential read back, byte-for-byte match. Verifies eviction + re-fetch.
6. `pagecache_stress`: tight loop of read/write/mmap-update interleaved, then verify final file content matches expected pattern. No torn pages.

### 9.3 Existing tests must still pass

`init`'s 71/71 must remain green. sbfs read/write tests, exec, fork tests all unchanged at API level.

## 10. Phase C — Write-Back (Future Follow-up)

When the user opens a follow-up session for Phase C, this section is the contract.

**Goal:** Eliminate per-`write` `writepage` call. Dirty pages flushed lazily.

**Required changes (incremental on top of Phase B):**

1. `generic_file_write`: drop the immediate `ops->writepage`; just set `p->dirty = 1`.
2. **Dirty list** in `page_cache.c`: a second linked list threading all dirty `pcache_page`s. `pcache_put` adds to dirty list when `dirty==1`; `writepage` removes.
3. **Eviction with dirty pages:** when LRU candidate is dirty, call `writepage` (sbfs: wrap in `begin_op`/`end_op`) before reuse. Single-txn-per-page is fine; per-op group batching is a Phase D optimisation.
4. **`sys_fsync(fd)` (new syscall):** flush all dirty pages of the inode behind `fd`, in order. begin_op/end_op around the batch.
5. **`sys_sync(void)` (new syscall):** walk global dirty list, flush all. Used by tests and shutdown.
6. **Shutdown flush:** kernel reboot/halt path calls `sys_sync` equivalent before stopping.
7. **Inode metadata:** `size` and `mtime` updates on extending writes must remain write-through (logged immediately). Otherwise a crash with a dirty data page already evicted leaves the inode pointing at uninitialised disk content. Concretely: extend size in the same `begin_op/end_op` as the block-allocation log entries, NOT inside `writepage`.
8. **Crash-safety review:** the sbfs log already provides idempotent block writes. Each `writepage` invocation must be wrapped in its own `begin_op/end_op` so the log invariants are not violated by mid-flush crashes.
9. **New tests:**
   - `fsync_test`: write, fsync, simulate crash via reboot, verify content present.
   - `sync_test`: write, sync, content visible to subsequent fresh-fd read.
   - `crash_recovery_pcache_test`: write without fsync, kernel panic-restart, verify either-old-or-new-content (not torn).

**Estimated effort:** ~half the size of Phase B. Most cache infrastructure reused. Highest-risk areas: log + writepage interaction inside eviction, and the metadata-write-through invariant.

## 11. Phase D — Further Future (out of scope for both B and C)

- Demand-grow page pool, watermark-driven eviction (option B from sizing brainstorm).
- Sequential-read read-ahead.
- Per-inode `mtime`/`ctime` updates on first dirty (currently only on extending write).
- Per-page wait queues so `pcache_get` can sleep instead of returning `-ENOMEM` when all slots pinned.

## 12. Files Touched (Phase B)

**New:**
- `kernel/include/page_cache.h`
- `kernel/page_cache.c`
- `bin/pagecache_test/pagecache_test.c`
- `bin/mmap_share_test/mmap_share_test.c`
- `bin/mmap_cow_test/mmap_cow_test.c`
- `bin/truncate_mmap_test/truncate_mmap_test.c`
- `bin/bigfile_pcache_test/bigfile_pcache_test.c`
- `bin/pagecache_stress/pagecache_stress.c`

**Modified:**
- `kernel/include/inode.h` (add `readpage`, `writepage` to `inode_ops`)
- `kernel/include/vma.h` (add `VMA_FLAG_SHARED`)
- `kernel/fs/vfs.c` (add `generic_file_read/write`, `generic_file_fault`)
- `kernel/include/vfs.h` (declare generic helpers)
- `kernel/fs/sbfs.c` (add `readpage`/`writepage`, refactor `read`/`write` to call generic)
- `kernel/fs/tarfs.c` (add `readpage`, refactor `read`)
- `kernel/syscall.c` (`sys_mmap` accept fd, `sys_msync` new, dispatch)
- `kernel/proc.c` (or fault file): file-VMA fault handling
- `kernel/kernel.c` (call `pcache_init` at boot)
- `libc/syscall.c`, `libc/include/sys/mman.h` (msync wrapper)
- `kernel/selftest.c` (pcache selftests)
- `bin/init/init.c` (run new userland tests)
- Build glue (`Makefile` and equivalents) for new bin/ targets.
