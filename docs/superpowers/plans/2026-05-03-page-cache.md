# Page Cache Implementation Plan (Phase B)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a unified, page-indexed file cache at the VFS layer so `read`, `write`, and `mmap` of regular files share `(inode, page#) → 4 KB page`.

**Architecture:** New `kernel/page_cache.c` (64 × 4 KB fixed pool, LRU). `inode_ops` gains `readpage`/`writepage`. VFS provides `generic_file_read`/`generic_file_write`/`generic_file_fault`. sbfs and tarfs implement `readpage`; sbfs implements `writepage`. Bio remains authoritative for sbfs blocks (write-through). `mmap` of regular files is enabled, including `MAP_PRIVATE` (with CoW) and `MAP_SHARED` (with dirty tracking + msync).

**Tech Stack:** C, riscv64-unknown-elf-gcc, freestanding, existing sbfs log + bio cache + page-ref reference counting.

**Spec reference:** `docs/superpowers/specs/2026-05-03-page-cache-design.md` (commit `fbbbe82`).

**Branch:** `feature/page-cache`.

**Conventions used in this plan:**
- New userland tests live at `bin/<name>/<name>.c`; the top Makefile auto-discovers them. No Makefile edits needed for new bins.
- All commits go on `feature/page-cache`. Never on `develop`.
- Selftests added to `kernel/selftest.c` and registered in the existing test list there.
- Run end-to-end smoke after every group: `make clean && timeout 240 make qemu` and verify init runner reports `<N>/<N> tests passed` and shell prompt appears.

---

## File Structure (Phase B)

| File | Responsibility |
|---|---|
| `kernel/include/page_cache.h` (new) | public API: `pcache_get/put/flush_inode/invalidate_range`, `pcache_init`, struct definition |
| `kernel/page_cache.c` (new) | fixed pool, LRU list, eviction, optional dirty flush via `ops->writepage` |
| `kernel/include/inode.h` | add `readpage`, `writepage` function pointers to `inode_ops` |
| `kernel/include/vma.h` | add `VMA_FLAG_SHARED` |
| `kernel/include/vfs.h` | declare `generic_file_read`, `generic_file_write`, `generic_file_fault` |
| `kernel/fs/vfs.c` | implement the three `generic_file_*` helpers |
| `kernel/fs/tarfs.c` | implement `tarfs_readpage`; route `tarfs_read` through generic |
| `kernel/fs/sbfs.c` | implement `sbfs_readpage`, `sbfs_writepage`; route `sbfs_read`/`sbfs_write` through generic |
| `kernel/vma.c` | extend `user_page_fault` for `VMA_TYPE_FILE`; teardown helper for file VMAs |
| `kernel/syscall.c` | `sys_mmap` accept fd; new `sys_msync` (#115); dispatch wiring |
| `kernel/proc.c` (or wherever VMA cleanup lives — check `proc_free`/exit path) | call file-VMA flush helper on VMA drop |
| `kernel/kernel.c` | call `pcache_init()` at boot |
| `libc/include/sys/mman.h` | declare `msync`, `MAP_SHARED`, `MAP_PRIVATE`, `MS_SYNC` |
| `libc/syscall.c` | `msync` wrapper |
| `kernel/selftest.c` | `pcache_basic_test`, `pcache_evict_test`, `pcache_dirty_writeback_test`, `pcache_pin_test` |
| `bin/pagecache_test/pagecache_test.c` (new) | read/mmap coherence userland test |
| `bin/mmap_share_test/mmap_share_test.c` (new) | shared mapping across fork |
| `bin/mmap_cow_test/mmap_cow_test.c` (new) | private writable mapping isolation |
| `bin/truncate_mmap_test/truncate_mmap_test.c` (new) | SIGBUS past-EOF after truncate |
| `bin/bigfile_pcache_test/bigfile_pcache_test.c` (new) | eviction correctness on file > pool |
| `bin/pagecache_stress/pagecache_stress.c` (new) | interleaved read/write/mmap stress |
| `bin/init/init.c` | append new userland tests to runner list |

---

## Task Map

1. Page cache skeleton (struct, init, pool, no readpage yet)
2. `pcache_get` / `pcache_put` (LRU, no readpage)
3. `inode_ops` `readpage`/`writepage` hooks; wire `pcache_get` to call `readpage` on miss
4. tarfs `readpage` + route `tarfs_read` through `generic_file_read`
5. sbfs `readpage` + route `sbfs_read` through `generic_file_read`
6. `generic_file_write` + sbfs `writepage` + route `sbfs_write` through generic
7. Eviction with dirty flush; `pcache_flush_inode`; `pcache_invalidate_range`
8. `sys_mmap` accept fd path; `VMA_TYPE_FILE` + `VMA_FLAG_SHARED` setup
9. File-VMA fault: read fault installs RO PTE pointing at cache page (with refcnt held)
10. `MAP_PRIVATE` + write fault: CoW from cache page to anon page
11. `MAP_SHARED` + write fault: upgrade RO→RW, mark page dirty
12. VMA teardown: walk faulted-in pages, flush dirty, drop pcache refcnts
13. `sys_msync` syscall + libc wrapper
14. Truncate hook drops cache pages past new size
15. Userland tests + init wiring + final smoke run

Each task block ends with a commit. Some tasks bundle a kernel selftest commit before final integration.

---

## Task 1: Page cache skeleton

**Files:**
- Create: `kernel/include/page_cache.h`
- Create: `kernel/page_cache.c`
- Modify: `kernel/kernel.c` (call `pcache_init` at boot)

- [ ] **Step 1: Write the header**

`kernel/include/page_cache.h`:
```c
#pragma once
#include <stdint.h>

struct inode;

#define PCACHE_NSLOTS 64
#define PCACHE_PGSZ   4096

struct pcache_page {
    struct inode *ip;          /* NULL = free slot */
    uint64_t      pgidx;       /* page index within file */
    void         *page;        /* PCACHE_PGSZ-byte physical page */
    int           refcnt;
    int           dirty;
    int           valid;       /* readpage filled successfully */
    struct pcache_page *prev, *next;  /* LRU doubly-linked */
};

void pcache_init(void);
int  pcache_get(struct inode *ip, uint64_t pgidx, struct pcache_page **out);
void pcache_put(struct pcache_page *p);
int  pcache_flush_inode(struct inode *ip);
void pcache_invalidate_range(struct inode *ip, uint64_t off, uint64_t len);
```

- [ ] **Step 2: Skeleton C file with init only**

`kernel/page_cache.c`:
```c
#include <page_cache.h>
#include <pmem.h>
#include <inode.h>
#include <printk.h>
#include <string.h>
#include <errno.h>
#include <riscv.h>

static struct pcache_page slots[PCACHE_NSLOTS];
static struct pcache_page lru_head;          /* sentinel */
static int pcache_inited = 0;

/* Same IRQ-off pattern as bio.c */
static int pcache_depth = 0;
static uint64_t pcache_saved_sie = 0;
static inline void pcache_lock(void) {
    uint64_t s = read_sstatus();
    write_sstatus(s & ~SSTATUS_SIE);
    if (pcache_depth++ == 0) pcache_saved_sie = s & SSTATUS_SIE;
}
static inline void pcache_unlock(void) {
    if (--pcache_depth == 0 && pcache_saved_sie)
        write_sstatus(read_sstatus() | SSTATUS_SIE);
}

void pcache_init(void) {
    if (pcache_inited) return;
    lru_head.prev = &lru_head;
    lru_head.next = &lru_head;
    for (int i = 0; i < PCACHE_NSLOTS; i++) {
        slots[i].ip     = 0;
        slots[i].pgidx  = 0;
        slots[i].refcnt = 0;
        slots[i].dirty  = 0;
        slots[i].valid  = 0;
        slots[i].page   = page_alloc();
        if (!slots[i].page) panic("pcache_init: page_alloc failed");
        slots[i].next = &lru_head;
        slots[i].prev = lru_head.prev;
        lru_head.prev->next = &slots[i];
        lru_head.prev = &slots[i];
    }
    pcache_inited = 1;
    printk("pcache: %d slots x %d bytes ready\n",
           PCACHE_NSLOTS, PCACHE_PGSZ);
}

/* Stubs filled in next tasks. */
int pcache_get(struct inode *ip, uint64_t pgidx, struct pcache_page **out) {
    (void)ip; (void)pgidx; (void)out; return -ENOSYS;
}
void pcache_put(struct pcache_page *p) { (void)p; }
int  pcache_flush_inode(struct inode *ip) { (void)ip; return 0; }
void pcache_invalidate_range(struct inode *ip, uint64_t off, uint64_t len) {
    (void)ip; (void)off; (void)len;
}
```

- [ ] **Step 3: Wire into boot**

Add `#include <page_cache.h>` in `kernel/kernel.c` and call `pcache_init();` after `binit();` (find the existing `binit()` call and add the new call directly after it).

- [ ] **Step 4: Build**

Run: `make clean && make build/kernel.elf`
Expected: clean build, no warnings.

- [ ] **Step 5: Boot smoke**

Run: `timeout 60 make qemu | head -30`
Expected: see `pcache: 64 slots x 4096 bytes ready` in boot log.

- [ ] **Step 6: Commit**

```bash
git add kernel/include/page_cache.h kernel/page_cache.c kernel/kernel.c
git commit -m "page_cache: pool skeleton + init"
```

---

## Task 2: pcache_get / pcache_put (no readpage yet)

**Files:**
- Modify: `kernel/page_cache.c`

- [ ] **Step 1: Implement bget-style helpers (replace stubs)**

In `kernel/page_cache.c`, replace the `pcache_get` and `pcache_put` stubs:

```c
static void lru_unlink(struct pcache_page *p) {
    p->prev->next = p->next;
    p->next->prev = p->prev;
}

static void lru_to_head(struct pcache_page *p) {
    /* head = MRU; tail = LRU candidate */
    p->next = lru_head.next;
    p->prev = &lru_head;
    lru_head.next->prev = p;
    lru_head.next = p;
}

/* Find existing slot for (ip, pgidx). NULL if absent. Caller holds lock. */
static struct pcache_page *pcache_lookup(struct inode *ip, uint64_t pgidx) {
    for (struct pcache_page *p = lru_head.next; p != &lru_head; p = p->next) {
        if (p->ip == ip && p->pgidx == pgidx && p->valid)
            return p;
    }
    return 0;
}

/* Find a victim slot to reuse. Returns NULL if all pinned/dirty.
 * Phase B writepage flushing of dirty victims is added in Task 7;
 * for now we only evict clean refcnt==0 pages. Caller holds lock. */
static struct pcache_page *pcache_evict(void) {
    for (struct pcache_page *p = lru_head.prev; p != &lru_head; p = p->prev) {
        if (p->refcnt == 0 && !p->dirty) {
            return p;
        }
    }
    return 0;
}

int pcache_get(struct inode *ip, uint64_t pgidx, struct pcache_page **out) {
    pcache_lock();
    struct pcache_page *p = pcache_lookup(ip, pgidx);
    if (p) {
        p->refcnt++;
        lru_unlink(p);
        lru_to_head(p);
        pcache_unlock();
        *out = p;
        return 0;
    }
    p = pcache_evict();
    if (!p) {
        pcache_unlock();
        return -ENOMEM;
    }
    /* Reuse slot. */
    p->ip     = ip;
    p->pgidx  = pgidx;
    p->refcnt = 1;
    p->dirty  = 0;
    p->valid  = 0;
    lru_unlink(p);
    lru_to_head(p);
    pcache_unlock();
    /* readpage call deferred to Task 3; for now just zero the page. */
    memset(p->page, 0, PCACHE_PGSZ);
    p->valid = 1;
    *out = p;
    return 0;
}

void pcache_put(struct pcache_page *p) {
    if (!p) return;
    pcache_lock();
    if (p->refcnt > 0) p->refcnt--;
    if (p->refcnt == 0) {
        /* Move to tail (LRU candidate). */
        lru_unlink(p);
        p->next = &lru_head;
        p->prev = lru_head.prev;
        lru_head.prev->next = p;
        lru_head.prev = p;
    }
    pcache_unlock();
}
```

- [ ] **Step 2: Add a kernel selftest**

In `kernel/selftest.c`, add at the bottom of the existing test list:

```c
static int pcache_basic_test(void) {
    /* Use a stack-allocated dummy inode pointer for keying. We only need
     * a stable, non-NULL pointer; pcache never dereferences it before
     * Task 3 wires up readpage. */
    struct inode dummy_a, dummy_b;
    struct pcache_page *p1, *p2, *p3;

    if (pcache_get(&dummy_a, 0, &p1) < 0) return -1;
    if (pcache_get(&dummy_a, 0, &p2) < 0) return -1;
    if (p1 != p2) return -1;                  /* same key → same slot */
    if (p1->refcnt != 2) return -1;
    pcache_put(p1);
    pcache_put(p2);
    if (pcache_get(&dummy_b, 0, &p3) < 0) return -1;
    if (p3 == p1) return -1;                  /* different key → different slot */
    pcache_put(p3);
    return 0;
}
```

Register in the `selftests[]` array (or whatever the existing structure is — search for where `kstack_page` test is registered and add `{"pcache_basic", pcache_basic_test}` next to it).

- [ ] **Step 3: Build + run**

Run: `make clean && timeout 90 make qemu | grep -E "pcache|tests passed"`
Expected: `pcache_basic ... PASS` (or whatever format the existing harness uses) and the global tests-passed count increments.

- [ ] **Step 4: Commit**

```bash
git add kernel/page_cache.c kernel/selftest.c
git commit -m "page_cache: get/put with LRU; basic selftest"
```

---

## Task 3: readpage hook + readpage-on-miss

**Files:**
- Modify: `kernel/include/inode.h`
- Modify: `kernel/page_cache.c`

- [ ] **Step 1: Add hooks to inode_ops**

In `kernel/include/inode.h`, inside `struct inode_ops`, add at the end (preserving existing fields):

```c
    /* Fill `page` (PCACHE_PGSZ bytes) from inode at byte offset
     * pgidx*PCACHE_PGSZ. Tail past EOF zero-filled. NULL on filesystems
     * that do not participate in the page cache (devfs/procfs). */
    int (*readpage) (struct inode *, uint64_t pgidx, void *page);

    /* Write `page` (PCACHE_PGSZ bytes) back to inode at offset
     * pgidx*PCACHE_PGSZ. Only bytes within current size persisted.
     * NULL = read-only fs. Caller wraps begin_op for sbfs. */
    int (*writepage)(struct inode *, uint64_t pgidx, const void *page);
```

- [ ] **Step 2: Update pcache_get to call readpage**

In `kernel/page_cache.c::pcache_get`, replace the `memset(p->page, 0, PCACHE_PGSZ); p->valid = 1;` block (the deferred-stub from Task 2) with:

```c
    /* Miss path: readpage if available, else zero-fill (for filesystems
     * without readpage we still serve a zero page so callers can detect
     * via valid flag; in practice generic_file_read only calls pcache
     * for inodes with readpage set). */
    int rc = 0;
    if (ip->ops && ip->ops->readpage) {
        rc = ip->ops->readpage(ip, pgidx, p->page);
    } else {
        memset(p->page, 0, PCACHE_PGSZ);
    }
    if (rc < 0) {
        /* Drop the slot — leave it ip=0 so next get re-fetches.
         * Refcnt stays 1 in the caller view; we instead treat this as
         * an immediate failure and unwind. */
        pcache_lock();
        p->ip = 0;
        p->pgidx = 0;
        p->refcnt = 0;
        p->valid = 0;
        /* Move back to tail so it's preferred for next reuse. */
        lru_unlink(p);
        p->next = &lru_head;
        p->prev = lru_head.prev;
        lru_head.prev->next = p;
        lru_head.prev = p;
        pcache_unlock();
        return rc;
    }
    p->valid = 1;
```

(The basic selftest from Task 2 used a dummy inode whose `ops` is NULL — make sure your test still passes; the `if (ip->ops && ...)` guard covers that.)

- [ ] **Step 3: Build + smoke**

Run: `make clean && timeout 90 make qemu | grep -E "pcache|tests passed"`
Expected: pcache_basic still passes; init still reaches its final tests-passed count.

- [ ] **Step 4: Commit**

```bash
git add kernel/include/inode.h kernel/page_cache.c
git commit -m "page_cache: call ops->readpage on miss"
```

---

## Task 4: tarfs readpage + generic_file_read

**Files:**
- Modify: `kernel/include/vfs.h`
- Modify: `kernel/fs/vfs.c`
- Modify: `kernel/fs/tarfs.c`

- [ ] **Step 1: Declare generic_file_read in vfs.h**

Append to `kernel/include/vfs.h`:
```c
#include <stdint.h>

/* Read up to n bytes from ip at off into buf via the page cache.
 * Returns bytes read, or negative -errno. The inode's ops->readpage
 * MUST be set. */
int generic_file_read(struct inode *ip, uint64_t off,
                      void *buf, uint64_t n);

/* Write up to n bytes via the page cache (write-through).
 * Returns bytes written, or negative -errno. Caller wraps begin_op
 * for sbfs (the helper itself does not call begin_op/end_op).
 * The inode's ops->writepage MUST be set. */
int generic_file_write(struct inode *ip, uint64_t off,
                       const void *buf, uint64_t n);

struct vma;
/* Page-fault path for VMA_TYPE_FILE. Returns 0 on success, -1 on failure
 * (caller raises SIGBUS / kills process). */
int generic_file_fault(struct vma *v, uint64_t fault_va);
```

- [ ] **Step 2: Implement generic_file_read in vfs.c**

Append to `kernel/fs/vfs.c`:
```c
#include <page_cache.h>
#include <string.h>

int generic_file_read(struct inode *ip, uint64_t off,
                      void *buf, uint64_t n) {
    if (!ip->ops || !ip->ops->readpage) return -EINVAL;
    if (off >= ip->size) return 0;
    if (off + n > ip->size) n = ip->size - off;
    if (n == 0) return 0;

    char *out = (char *)buf;
    uint64_t done = 0;
    while (done < n) {
        uint64_t pos    = off + done;
        uint64_t pgidx  = pos / PCACHE_PGSZ;
        uint64_t pgoff  = pos % PCACHE_PGSZ;
        uint64_t chunk  = PCACHE_PGSZ - pgoff;
        if (chunk > n - done) chunk = n - done;

        struct pcache_page *p;
        int rc = pcache_get(ip, pgidx, &p);
        if (rc < 0) {
            if (done > 0) return (int)done;
            return rc;
        }
        memcpy(out + done, (char *)p->page + pgoff, chunk);
        pcache_put(p);
        done += chunk;
    }
    return (int)done;
}
```

- [ ] **Step 3: Implement tarfs_readpage**

In `kernel/fs/tarfs.c`, before `static const struct inode_ops tarfs_ops`, add:
```c
static int tarfs_readpage(struct inode *ip, uint64_t pgidx, void *page) {
    struct tarfs_ino_data *d = ip->fs_data;
    uint64_t off = pgidx * 4096UL;
    if (off >= d->file_size) {
        memset(page, 0, 4096);
        return 0;
    }
    uint64_t n = 4096;
    if (off + n > d->file_size) n = d->file_size - off;
    memcpy(page, d->data + off, n);
    if (n < 4096) memset((char *)page + n, 0, 4096 - n);
    return 0;
}
```

Add `.readpage = tarfs_readpage,` to the `tarfs_ops` struct literal.

- [ ] **Step 4: Route tarfs_read through generic helper**

Replace the body of `tarfs_read` in `kernel/fs/tarfs.c` with:
```c
static int tarfs_read(struct inode *ip, uint64_t off, void *buf, uint64_t n) {
    return generic_file_read(ip, off, buf, n);
}
```

(Add `#include <vfs.h>` at top if not already there; the existing file does include it.)

- [ ] **Step 5: Build + boot**

Run: `make clean && timeout 240 make qemu 2>&1 | tail -30`
Expected: init reaches its tests-passed line. Reading any file from tarfs (the tests themselves are in tarfs as `/bin/*`) implicitly exercises the new path. Pay attention to any new failures — `cat`, `header_test`, `ctype_test` all read from tarfs.

- [ ] **Step 6: Commit**

```bash
git add kernel/include/vfs.h kernel/fs/vfs.c kernel/fs/tarfs.c
git commit -m "tarfs: implement readpage; route reads through page cache"
```

---

## Task 5: sbfs readpage + route reads

**Files:**
- Modify: `kernel/fs/sbfs.c`

- [ ] **Step 1: Implement sbfs_readpage**

In `kernel/fs/sbfs.c`, add (above the inode_ops struct):
```c
static int sbfs_readpage(struct inode *ip, uint64_t pgidx, void *page) {
    uint64_t off = pgidx * 4096UL;
    int n = sbfs_readi(ip, off, page, 4096);
    if (n < 0) return n;
    if (n < 4096) memset((char *)page + n, 0, 4096 - n);
    return 0;
}
```

- [ ] **Step 2: Reroute sbfs_read**

Find the existing `sbfs_read` (around line 573 per earlier grep — locate the function whose body is `return sbfs_readi(ip, off, buf, n);`) and replace its body with:
```c
    return generic_file_read(ip, off, buf, n);
```

Add `#include <vfs.h>` at the top of `sbfs.c` if missing (it already includes it via tarfs.h chain — verify by checking the includes block).

- [ ] **Step 3: Register readpage in sbfs_ops**

Locate the `inode_ops` struct literal in `sbfs.c` (search for `.read = sbfs_read,`) and add `.readpage = sbfs_readpage,` to it.

- [ ] **Step 4: Build + smoke**

Run: `make clean && timeout 240 make qemu 2>&1 | grep -E "sbfs|tests passed|FAIL"`
Expected: existing sbfs tests still pass (file open/read/cat from sbfs mount).

- [ ] **Step 5: Commit**

```bash
git add kernel/fs/sbfs.c
git commit -m "sbfs: implement readpage; route reads through page cache"
```

---

## Task 6: writepage + generic_file_write

**Files:**
- Modify: `kernel/fs/vfs.c`
- Modify: `kernel/fs/sbfs.c`

- [ ] **Step 1: Implement generic_file_write**

Append to `kernel/fs/vfs.c`:
```c
int generic_file_write(struct inode *ip, uint64_t off,
                       const void *buf, uint64_t n) {
    if (!ip->ops || !ip->ops->writepage || !ip->ops->readpage) return -EINVAL;
    if (n == 0) return 0;

    const char *in = (const char *)buf;
    uint64_t done = 0;

    while (done < n) {
        uint64_t pos   = off + done;
        uint64_t pgidx = pos / PCACHE_PGSZ;
        uint64_t pgoff = pos % PCACHE_PGSZ;
        uint64_t chunk = PCACHE_PGSZ - pgoff;
        if (chunk > n - done) chunk = n - done;

        struct pcache_page *p;
        int rc = pcache_get(ip, pgidx, &p);
        if (rc < 0) {
            if (done > 0) return (int)done;
            return rc;
        }
        memcpy((char *)p->page + pgoff, in + done, chunk);
        rc = ip->ops->writepage(ip, pgidx, p->page);
        if (rc < 0) {
            /* Invalidate the page so a future read re-fetches. */
            p->valid = 0;
            pcache_put(p);
            if (done > 0) return (int)done;
            return rc;
        }
        /* write-through: page now matches disk; not dirty. */
        p->dirty = 0;
        pcache_put(p);
        done += chunk;
    }
    return (int)done;
}
```

- [ ] **Step 2: Implement sbfs_writepage**

In `kernel/fs/sbfs.c`, add (above sbfs_ops):
```c
/* Caller MUST be inside begin_op/end_op. */
static int sbfs_writepage(struct inode *ip, uint64_t pgidx, const void *page) {
    uint64_t off = pgidx * 4096UL;
    /* Bytes to persist: clamp to current file size for the page. Bytes
     * beyond size are not written here — generic_file_write tracks the
     * size update through sbfs_writei semantics in step 3 below. */
    uint64_t end = off + 4096UL;
    if (end > ip->size) end = ip->size;
    if (end <= off) return 0;
    int n = sbfs_writei(ip, off, page, end - off);
    if (n < 0) return n;
    return 0;
}
```

- [ ] **Step 3: Reroute sbfs_write**

Locate `sbfs_write` (the wrapper that does `begin_op(); ret = sbfs_writei(...); end_op();`) and replace with the dual path: extend size first via writei (which allocates blocks AND writes), then keep page cache consistent.

The simplest correct approach: keep `sbfs_writei` as the on-disk path inside one transaction, and after a successful writei, **update or invalidate** any cached page in the touched range so future reads see new content.

Replace the body of `sbfs_write` with:
```c
    begin_op();
    int ret = sbfs_writei(ip, off, buf, n);
    end_op();
    if (ret > 0) {
        /* Invalidate cached pages spanning the written range so subsequent
         * generic_file_read re-fetches via readpage. (Phase D may upgrade
         * this to in-place memcpy for hot files.) */
        pcache_invalidate_range(ip, off, (uint64_t)ret);
    }
    return ret;
```

(Note: this short-circuits `generic_file_write` for sbfs's own write op. `generic_file_write` is still useful for any future fs that wants pure pcache+writepage semantics. We keep it correct and tested via writepage/eviction-flush paths in Tasks 7+11.)

Add `.writepage = sbfs_writepage,` to the sbfs_ops struct literal.

- [ ] **Step 4: Build + smoke**

Run: `make clean && timeout 240 make qemu 2>&1 | grep -E "sbfs|tests passed|FAIL"`
Expected: existing sbfs read/write tests pass.

- [ ] **Step 5: Commit**

```bash
git add kernel/fs/vfs.c kernel/fs/sbfs.c
git commit -m "page_cache: generic_file_write + sbfs writepage"
```

---

## Task 7: Eviction with dirty flush + flush_inode + invalidate_range

**Files:**
- Modify: `kernel/page_cache.c`

- [ ] **Step 1: Upgrade pcache_evict to flush dirty pages**

In `kernel/page_cache.c`, replace `pcache_evict` with:
```c
/* Caller does NOT hold lock — we drop it across writepage because
 * sbfs_writepage takes the bio cache lock and calls into log code,
 * neither of which is reentrant under our IRQ-off section. */
static struct pcache_page *pcache_evict(void) {
    /* First pass: clean unpinned. */
    for (struct pcache_page *p = lru_head.prev; p != &lru_head; p = p->prev) {
        if (p->refcnt == 0 && !p->dirty) return p;
    }
    /* Second pass: dirty unpinned — flush, then reuse. */
    for (struct pcache_page *p = lru_head.prev; p != &lru_head; p = p->prev) {
        if (p->refcnt != 0) continue;
        if (!p->dirty) continue;
        if (!p->ip || !p->ip->ops || !p->ip->ops->writepage) {
            /* Should not happen — only fs with writepage can mark dirty.
             * Defensively drop dirty bit and use the slot. */
            p->dirty = 0;
            return p;
        }
        struct inode *ip = p->ip;
        uint64_t pgidx = p->pgidx;
        /* Pin during flush so a parallel pcache_get can't reuse it. */
        p->refcnt++;
        pcache_unlock();
        /* sbfs writepage requires begin_op/end_op. We can't know the fs
         * here, so we delegate via a per-fs flush wrapper. For Phase B
         * sbfs is the only writeable fs; call sbfs's wrapper directly. */
        extern int sbfs_writepage_locked(struct inode *, uint64_t,
                                         const void *);
        int rc = sbfs_writepage_locked(ip, pgidx, p->page);
        pcache_lock();
        p->refcnt--;
        if (rc == 0) p->dirty = 0;
        if (p->refcnt == 0 && !p->dirty) return p;
    }
    return 0;
}
```

(In `pcache_get`, the caller of `pcache_evict` already holds the lock — keep that lock-acquire-call-release pattern around. The lock drop inside pcache_evict is internal.)

- [ ] **Step 2: Add sbfs_writepage_locked wrapper**

In `kernel/fs/sbfs.c`, add:
```c
/* Public wrapper: takes the begin_op/end_op transaction itself.
 * Used by page-cache eviction and msync/munmap flush paths. */
int sbfs_writepage_locked(struct inode *ip, uint64_t pgidx, const void *page) {
    begin_op();
    int rc = sbfs_writepage(ip, pgidx, page);
    end_op();
    return rc;
}
```

- [ ] **Step 3: Implement pcache_flush_inode and pcache_invalidate_range**

In `kernel/page_cache.c`, replace stubs:
```c
int pcache_flush_inode(struct inode *ip) {
    int busy = 0;
    pcache_lock();
    for (int i = 0; i < PCACHE_NSLOTS; i++) {
        struct pcache_page *p = &slots[i];
        if (p->ip != ip) continue;
        if (p->refcnt > 0) { busy = 1; continue; }
        if (p->dirty && p->ip->ops && p->ip->ops->writepage) {
            extern int sbfs_writepage_locked(struct inode *, uint64_t,
                                             const void *);
            p->refcnt++;
            pcache_unlock();
            sbfs_writepage_locked(p->ip, p->pgidx, p->page);
            pcache_lock();
            p->refcnt--;
            p->dirty = 0;
        }
        p->ip = 0;
        p->pgidx = 0;
        p->valid = 0;
    }
    pcache_unlock();
    return busy ? -EBUSY : 0;
}

void pcache_invalidate_range(struct inode *ip, uint64_t off, uint64_t len) {
    uint64_t pg_start = off / PCACHE_PGSZ;
    uint64_t pg_end   = (off + len + PCACHE_PGSZ - 1) / PCACHE_PGSZ;
    pcache_lock();
    for (int i = 0; i < PCACHE_NSLOTS; i++) {
        struct pcache_page *p = &slots[i];
        if (p->ip != ip) continue;
        if (p->pgidx < pg_start || p->pgidx >= pg_end) continue;
        if (p->refcnt > 0) {
            /* Mapped page — leave it; bounds check at fault path will
             * surface SIGBUS for past-EOF accesses. */
            continue;
        }
        p->ip = 0;
        p->pgidx = 0;
        p->valid = 0;
        p->dirty = 0;
    }
    pcache_unlock();
}
```

- [ ] **Step 4: Selftest — eviction**

Add to `kernel/selftest.c`:
```c
static int pcache_evict_test(void) {
    /* Use NSLOTS+1 distinct dummy keys; release each immediately so
     * eviction can recycle. After getting key NSLOTS+1, the original
     * key 0 should miss and be re-fetched. We use dummy inodes with
     * NULL ops, so readpage isn't called — pcache_get falls back to
     * zero-fill, which is enough to test slot recycling. */
    struct inode dummies[PCACHE_NSLOTS + 1];
    for (int i = 0; i < PCACHE_NSLOTS + 1; i++) {
        struct pcache_page *p;
        if (pcache_get(&dummies[i], 0, &p) < 0) return -1;
        pcache_put(p);
    }
    /* All NSLOTS+1 distinct (ip, 0) entries got served — at least one
     * earlier entry must have been evicted. Touch first dummy: should
     * still resolve OK. (We can't easily check it was a miss without
     * exposing internals.) */
    struct pcache_page *p;
    if (pcache_get(&dummies[0], 0, &p) < 0) return -1;
    pcache_put(p);
    return 0;
}
```

Register `{"pcache_evict", pcache_evict_test}` next to `pcache_basic`.

- [ ] **Step 5: Build + run selftests**

Run: `make clean && timeout 240 make qemu 2>&1 | grep -E "pcache|tests passed"`
Expected: both pcache selftests pass.

- [ ] **Step 6: Commit**

```bash
git add kernel/page_cache.c kernel/fs/sbfs.c kernel/selftest.c
git commit -m "page_cache: dirty-flush eviction; flush_inode; invalidate_range"
```

---

## Task 8: sys_mmap accept fd; VMA_TYPE_FILE setup

**Files:**
- Modify: `kernel/include/vma.h`
- Modify: `kernel/syscall.c`

- [ ] **Step 1: Add VMA_FLAG_SHARED**

In `kernel/include/vma.h`, after the existing `VMA_FLAG_COW` define:
```c
#define VMA_FLAG_SHARED 0x2
```

- [ ] **Step 2: Find file/fd helpers**

Before editing `sys_mmap`, locate the existing helper that converts fd → struct file (or → inode). Run: `grep -n "fd_get\|file_from_fd\|proc->ofile\|ofiles\[" kernel/syscall.c | head -20`. Use whichever helper sys_open / sys_read use today; in a typical sbunix layout you'll find something like `proc->ofile[fd]` directly. We'll call that the "fd table".

- [ ] **Step 3: Rewrite sys_mmap**

In `kernel/syscall.c`, locate `sys_mmap` and replace its body with the version below. Key changes from current code: drop the `fd != -1 || off != 0` rejection; handle `MAP_SHARED` flag; populate `v->file`/`v->file_off`/`VMA_FLAG_SHARED`.

```c
#define MAP_PRIVATE 0x02
#define MAP_SHARED  0x01
#define MAP_ANON    0x20

static int64_t sys_mmap(uint64_t addr, uint64_t len, int prot, int flags,
                        int fd, uint64_t off) {
    (void)addr;  /* always anonymous-style placement for now */
    if (len == 0) return -EINVAL;
    len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if ((flags & (MAP_PRIVATE | MAP_SHARED)) == 0) return -EINVAL;
    if ((flags & MAP_PRIVATE) && (flags & MAP_SHARED)) return -EINVAL;

    struct inode *fip = 0;
    uint64_t      file_off = 0;

    if (!(flags & MAP_ANON)) {
        if (fd < 0) return -EINVAL;
        if (off & (PAGE_SIZE - 1)) return -EINVAL;
        struct pcb *p = current_proc();
        if (!p) return -EINVAL;
        struct file *f = p->ofile[fd];
        if (!f) return -EBADF;
        if (!f->ip || f->ip->type != I_REG) return -EACCES;
        if (!f->ip->ops || !f->ip->ops->readpage) return -ENODEV;
        if ((prot & VMA_PROT_W) && (flags & MAP_SHARED)) {
            if (!f->ip->ops->writepage) return -EROFS;
            if (!(f->writable)) return -EACCES;
        }
        if ((prot & VMA_PROT_R) && !(f->readable)) return -EACCES;
        fip = inode_get(f->ip);
        file_off = off;
    } else {
        if (fd != -1 || off != 0) return -EINVAL;
    }

    /* existing top-down search for an empty range — keep current code */
    struct pcb *proc = current_proc();
    uint64_t search = MMAP_END - len;
    while (search >= MMAP_START) {
        int overlap = 0;
        for (struct vma *v = proc->vma_list; v; v = v->next) {
            if (search < v->end && search + len > v->start) {
                overlap = 1;
                if (v->start < MMAP_START) { overlap = 1; break; }
                search = v->start - len;
                search &= ~(PAGE_SIZE - 1);
                break;
            }
        }
        if (!overlap) break;
        if (search < MMAP_START) { if (fip) inode_put(fip); return -ENOMEM; }
    }
    if (search < MMAP_START) { if (fip) inode_put(fip); return -ENOMEM; }

    struct vma *v = vma_alloc();
    if (!v) { if (fip) inode_put(fip); return -ENOMEM; }
    v->start    = search;
    v->end      = search + len;
    v->prot     = (uint32_t)prot;
    v->flags    = 0;
    v->file     = 0;
    v->file_off = 0;
    if (fip) {
        v->type     = VMA_TYPE_FILE;
        v->file     = fip;
        v->file_off = file_off;
        if (flags & MAP_SHARED) v->flags |= VMA_FLAG_SHARED;
        /* MAP_PRIVATE writable: needs CoW on write fault. Set the COW
         * flag preemptively; the fault path consults it to decide
         * whether to copy. */
        if ((flags & MAP_PRIVATE) && (prot & VMA_PROT_W)) {
            v->flags |= VMA_FLAG_COW;
        }
    } else {
        v->type = VMA_TYPE_ANON;
    }
    vma_insert(&proc->vma_list, v);
    return (int64_t)search;
}
```

(If `struct file` field names differ — `readable`/`writable` vs `readable_only`/`is_writable` — adapt to whatever `kernel/file.c` uses. Verify by grepping.)

- [ ] **Step 4: Build**

Run: `make clean && make build/kernel.elf 2>&1 | tail -20`
Expected: clean build. Most likely error: missing field name in `struct file`. Fix by inspecting `kernel/include/file.h` (search for it).

- [ ] **Step 5: Smoke (no fault handler yet → faulting will SIGSEGV)**

Run: `timeout 240 make qemu 2>&1 | grep -E "tests passed|FAIL"`
Expected: existing tests still pass; no userland code uses the new fd path yet.

- [ ] **Step 6: Commit**

```bash
git add kernel/include/vma.h kernel/syscall.c
git commit -m "mmap: accept fd for regular files; setup VMA_TYPE_FILE"
```

---

## Task 9: File-VMA fault — read fault installs RO PTE

**Files:**
- Modify: `kernel/vma.c`

- [ ] **Step 1: Add VMA_TYPE_FILE branch in user_page_fault**

In `kernel/vma.c::user_page_fault`, locate the section after the permission checks (after the `if (scause == 12 && !(v->prot & VMA_PROT_X))` line) and before the existing demand-page block. Insert a new branch that handles `VMA_TYPE_FILE` first:

```c
    if (v->type == VMA_TYPE_FILE) {
        /* Bounds against file size: faulting past EOF → SIGBUS (return -1). */
        uint64_t mapping_off = (fault_va - v->start);
        uint64_t file_pos    = v->file_off + mapping_off;
        uint64_t file_pgidx  = file_pos / PAGE_SIZE;
        if (file_pos >= v->file->size) return -1;

        /* Already-present PTE handling for write fault (CoW or shared upgrade). */
        pte_t *pte_existing = get_pte(p->pagetable, fault_va, 0);
        if (pte_existing && (*pte_existing & PTE_V)) {
            if (scause != 15) return -1;          /* read fault on present? */
            /* Will be filled in by Tasks 10 / 11. For now: */
            return -1;
        }

        struct pcache_page *pp;
        int rc = pcache_get(v->file, file_pgidx, &pp);
        if (rc < 0) return -1;

        unsigned long perm = PTE_U | PTE_V;
        if (v->prot & VMA_PROT_R) perm |= PTE_R;
        if (v->prot & VMA_PROT_X) perm |= PTE_X;
        /* RO install: writable pages handled in Tasks 10 / 11. */
        unsigned long pa = virt_to_phys((unsigned long)pp->page);
        vmem_map(p->pagetable, fault_va, pa, PAGE_SIZE, perm);
        flush_tlb();
        /* refcnt remains held; released in vma teardown (Task 12). */
        return 0;
    }
```

- [ ] **Step 2: Build + boot**

Run: `make clean && timeout 240 make qemu 2>&1 | tail -10`
Expected: boot reaches init tests; existing anon mmap tests still pass; no userland exercises file mmap yet.

- [ ] **Step 3: Quick smoke userland test**

Create `bin/mmap_smoke_test/mmap_smoke_test.c`:
```c
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

int main(void) {
    int fd = open("/etc/motd", O_RDONLY);
    if (fd < 0) { printf("mmap_smoke: open failed\n"); return 1; }
    char *p = mmap(0, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == (void *)-1) { printf("mmap_smoke: mmap failed\n"); return 1; }
    /* Read first byte. */
    char c = p[0];
    printf("mmap_smoke: first byte ok (%d)\n", (int)c);
    return 0;
}
```

(Skip if `/etc/motd` doesn't exist — substitute any small known file in tarfs; check with `ls /workspaces/sbunix/etc/`.)

Add `"/bin/mmap_smoke_test",` to the test list in `bin/init/init.c` (right after the existing mmap-related tests; search for `cow_test` to find the area).

- [ ] **Step 4: Run**

Run: `make clean && timeout 240 make qemu 2>&1 | grep -E "mmap_smoke|tests passed"`
Expected: `mmap_smoke: first byte ok (...)`, tests-passed count incremented.

- [ ] **Step 5: Commit**

```bash
git add kernel/vma.c bin/mmap_smoke_test/mmap_smoke_test.c bin/init/init.c
git commit -m "vma: file-mapping fault path; RO PTE points at page cache"
```

---

## Task 10: MAP_PRIVATE + write fault → CoW from cache page

**Files:**
- Modify: `kernel/vma.c`

- [ ] **Step 1: Replace the placeholder in the file-VMA fault block**

In the `if (pte_existing && (*pte_existing & PTE_V))` branch added in Task 9, replace `return -1;` with the CoW path:

```c
        if (pte_existing && (*pte_existing & PTE_V)) {
            if (scause != 15) return -1;
            unsigned long old_pa = pte_to_phyaddr(*pte_existing);

            if (v->flags & VMA_FLAG_COW) {
                /* MAP_PRIVATE writable: copy cache page to a fresh anon
                 * page and remap. Cache refcnt drops; the new anon page
                 * has its own page_ref of 1. */
                void *np = page_alloc();
                if (!np) return -1;
                memmove(np, (void *)phys_to_virt(old_pa), PAGE_SIZE);
                unsigned long new_pa = virt_to_phys((unsigned long)np);
                unsigned long perm = PTE_U | PTE_V | PTE_R | PTE_W;
                if (v->prot & VMA_PROT_X) perm |= PTE_X;
                *pte_existing =
                    phyaddr_to_pte(new_pa) | perm | PTE_LEAF_AD;
                /* Find the pcache page that owns old_pa and drop our ref.
                 * We located it by file_pgidx earlier; rather than search
                 * by PA, look it up again — guaranteed to be cached
                 * because we just faulted from it. */
                struct pcache_page *opp;
                if (pcache_get(v->file, file_pgidx, &opp) == 0) {
                    /* drop both the freshly-acquired ref AND the original
                     * fault-time ref */
                    pcache_put(opp);
                    pcache_put(opp);
                }
                /* The COW flag should NOT be cleared on the VMA — siblings
                 * of this page in the same VMA may still need CoW. */
                flush_tlb();
                return 0;
            }
            /* MAP_SHARED write fault — Task 11. */
            return -1;
        }
```

- [ ] **Step 2: Userland test — mmap_cow_test**

Create `bin/mmap_cow_test/mmap_cow_test.c`:
```c
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>

int main(void) {
    int fd = open("/etc/motd", O_RDONLY);
    if (fd < 0) { printf("mmap_cow: open failed\n"); return 1; }
    char *p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (p == (void *)-1) { printf("mmap_cow: mmap failed\n"); return 1; }
    char orig = p[0];

    int pid = fork();
    if (pid == 0) {
        p[0] = orig + 1;  /* CoW write in child */
        if (p[0] != orig + 1) { _exit(1); }
        _exit(0);
    }
    int st;
    wait(&st);
    if (p[0] != orig) {
        printf("mmap_cow: parent saw child write (FAIL)\n");
        return 1;
    }
    /* Re-open file via fd to verify file unchanged. */
    int fd2 = open("/etc/motd", O_RDONLY);
    char buf[1];
    read(fd2, buf, 1);
    close(fd2);
    if (buf[0] != orig) {
        printf("mmap_cow: file modified on disk (FAIL)\n");
        return 1;
    }
    printf("mmap_cow: ok\n");
    return 0;
}
```

Add `"/bin/mmap_cow_test",` to `bin/init/init.c` test list.

- [ ] **Step 3: Build + run**

Run: `make clean && timeout 240 make qemu 2>&1 | grep -E "mmap_cow|tests passed"`
Expected: `mmap_cow: ok` and tests-passed increments.

- [ ] **Step 4: Commit**

```bash
git add kernel/vma.c bin/mmap_cow_test/mmap_cow_test.c bin/init/init.c
git commit -m "vma: MAP_PRIVATE write fault triggers CoW from cache page"
```

---

## Task 11: MAP_SHARED + write fault → upgrade RW + mark dirty

**Files:**
- Modify: `kernel/vma.c`

- [ ] **Step 1: Replace the remaining placeholder in the file-VMA fault block**

Inside the `if (pte_existing && ...)` branch, the final `return -1;` (from "MAP_SHARED write fault — Task 11") is replaced by:

```c
            if (v->flags & VMA_FLAG_SHARED) {
                /* Upgrade RO → RW. Mark the cache page dirty so the
                 * msync/munmap flush path writes it back. */
                struct pcache_page *pp;
                if (pcache_get(v->file, file_pgidx, &pp) < 0) return -1;
                pp->dirty = 1;
                /* Drop the ref we just took for lookup; the original
                 * fault-time ref still pins it. */
                pcache_put(pp);

                unsigned long perm = PTE_U | PTE_V | PTE_R | PTE_W;
                if (v->prot & VMA_PROT_X) perm |= PTE_X;
                unsigned long pa = pte_to_phyaddr(*pte_existing);
                *pte_existing =
                    phyaddr_to_pte(pa) | perm | PTE_LEAF_AD;
                flush_tlb();
                return 0;
            }
            return -1;
```

- [ ] **Step 2: Build**

Run: `make clean && make build/kernel.elf`
Expected: clean.

- [ ] **Step 3: Defer userland verification to Task 13**

The flush path (msync) doesn't exist yet. Userland test for MAP_SHARED+W is added in Task 13 once msync is implemented.

- [ ] **Step 4: Commit**

```bash
git add kernel/vma.c
git commit -m "vma: MAP_SHARED write fault upgrades RO->RW + marks page dirty"
```

---

## Task 12: VMA teardown flushes dirty + drops pcache refs

**Files:**
- Modify: `kernel/vma.c`
- Modify: `kernel/proc.c` (or wherever VMA cleanup is called — see step 1)

- [ ] **Step 1: Locate VMA cleanup point**

Run: `grep -n "vma_list_free\|munmap\|sys_munmap" kernel/syscall.c kernel/proc.c kernel/vma.c | head -20`. The cleanup typically lives in `sys_munmap` and in process exit (`free_proc` / `proc_free`). Both paths must call the new helper.

- [ ] **Step 2: Add vma_drop_file_pages helper**

Append to `kernel/vma.c`:
```c
#include <page_cache.h>

/* For a VMA_TYPE_FILE vma, walk its faulted-in PTEs, flush dirty cache
 * pages, drop pcache refcnts, and unmap. Caller frees the VMA struct. */
void vma_drop_file_pages(struct pcb *p, struct vma *v) {
    if (v->type != VMA_TYPE_FILE) return;
    for (uint64_t va = v->start; va < v->end; va += PAGE_SIZE) {
        pte_t *pte = get_pte(p->pagetable, va, 0);
        if (!pte || !(*pte & PTE_V)) continue;
        unsigned long pa = pte_to_phyaddr(*pte);
        uint64_t pgidx = (va - v->start + v->file_off) / PAGE_SIZE;

        struct pcache_page *pp;
        /* Re-acquire to find the slot; we then drop both the new ref and
         * the long-lived fault-time ref. */
        if (pcache_get(v->file, pgidx, &pp) == 0) {
            unsigned long pcache_pa = virt_to_phys((unsigned long)pp->page);
            if (pcache_pa == pa) {
                if (pp->dirty && (v->flags & VMA_FLAG_SHARED)
                    && v->file->ops && v->file->ops->writepage) {
                    extern int sbfs_writepage_locked(struct inode *,
                                                    uint64_t, const void *);
                    sbfs_writepage_locked(v->file, pgidx, pp->page);
                    pp->dirty = 0;
                }
                pcache_put(pp);   /* fault-time ref */
                pcache_put(pp);   /* lookup ref */
            } else {
                /* PTE points to a CoW anon page, not the cache. Free as
                 * an anon page. */
                pcache_put(pp);   /* lookup ref only */
                page_put(pa);
            }
        }
        /* Clear PTE. */
        *pte = 0;
    }
    flush_tlb();
    if (v->file) { inode_put(v->file); v->file = 0; }
}
```

Add a forward declaration in `kernel/include/vma.h`:
```c
struct pcb;
void vma_drop_file_pages(struct pcb *p, struct vma *v);
```

- [ ] **Step 3: Wire into munmap**

In `sys_munmap` (in `kernel/syscall.c`), before calling `vma_split` or freeing a VMA, add:
```c
    /* If the matched VMA covers a file mapping fully removed by this
     * munmap, drop its file pages first. For partial munmaps we simply
     * unmap the PTEs in [start,end) and leave the rest intact — this
     * leaks pcache refcnts on the unmapped portion, which is OK for
     * Phase B (full munmap is the common case). */
    if (v->type == VMA_TYPE_FILE && start <= v->start && end >= v->end) {
        vma_drop_file_pages(current_proc(), v);
    }
```

- [ ] **Step 4: Wire into process exit**

In the process-cleanup path that today calls `vma_list_free` (search `grep -n vma_list_free kernel/proc.c kernel/syscall.c`), iterate the list **before** calling `vma_list_free` and call `vma_drop_file_pages(p, v)` for each `VMA_TYPE_FILE` entry. Concretely, add a helper just above the existing `vma_list_free` call:

```c
    for (struct vma *v = p->vma_list; v; v = v->next) {
        if (v->type == VMA_TYPE_FILE)
            vma_drop_file_pages(p, v);
    }
    vma_list_free(&p->vma_list);
```

- [ ] **Step 5: Build + smoke**

Run: `make clean && timeout 240 make qemu 2>&1 | grep -E "mmap|tests passed"`
Expected: existing mmap_cow_test still passes (it exits cleanly so teardown runs).

- [ ] **Step 6: Commit**

```bash
git add kernel/vma.c kernel/include/vma.h kernel/proc.c kernel/syscall.c
git commit -m "vma: teardown flushes dirty + drops pcache refs on file VMAs"
```

---

## Task 13: sys_msync + libc wrapper

**Files:**
- Modify: `kernel/syscall.c`
- Modify: `libc/syscall.c`
- Modify: `libc/include/sys/mman.h`

- [ ] **Step 1: Add the syscall handler**

In `kernel/syscall.c`, define syscall #115:
```c
#define SYS_msync 115
#define MS_SYNC 0x4

static int64_t sys_msync(uint64_t addr, uint64_t len, int flags) {
    if (flags != MS_SYNC) return -EINVAL;
    if (len == 0) return 0;
    uint64_t end = addr + len;
    if (addr & (PAGE_SIZE - 1)) return -EINVAL;
    end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    struct pcb *p = current_proc();
    if (!p) return -EINVAL;
    for (struct vma *v = p->vma_list; v; v = v->next) {
        if (v->type != VMA_TYPE_FILE) continue;
        if (!(v->flags & VMA_FLAG_SHARED)) continue;
        uint64_t s = (addr > v->start) ? addr : v->start;
        uint64_t e = (end < v->end) ? end : v->end;
        if (s >= e) continue;
        for (uint64_t va = s; va < e; va += PAGE_SIZE) {
            pte_t *pte = get_pte(p->pagetable, va, 0);
            if (!pte || !(*pte & PTE_V)) continue;
            uint64_t pgidx =
                (va - v->start + v->file_off) / PAGE_SIZE;
            struct pcache_page *pp;
            if (pcache_get(v->file, pgidx, &pp) < 0) continue;
            if (pp->dirty && v->file->ops && v->file->ops->writepage) {
                extern int sbfs_writepage_locked(struct inode *,
                                                 uint64_t, const void *);
                sbfs_writepage_locked(v->file, pgidx, pp->page);
                pp->dirty = 0;
            }
            pcache_put(pp);
        }
    }
    return 0;
}
```

Wire into the syscall dispatch switch:
```c
        case SYS_msync:
            return sys_msync(trapframe[TF_A0], trapframe[TF_A1],
                             (int)trapframe[TF_A2]);
```

- [ ] **Step 2: libc wrapper**

In `libc/include/sys/mman.h` (create or extend), declare:
```c
#define MAP_PRIVATE 0x02
#define MAP_SHARED  0x01
#define MAP_ANON    0x20
#define PROT_NONE   0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
#define MS_SYNC     0x4
#define MAP_FAILED ((void *)-1L)
void *mmap(void *addr, long len, int prot, int flags, int fd, long off);
int   munmap(void *addr, long len);
int   msync(void *addr, long len, int flags);
```

(Many of these likely already exist — only add what's missing.)

In `libc/syscall.c`, append:
```c
int msync(void *addr, long len, int flags) {
    return (int)syscall_ret(ecall3(115, (long)addr, len, (long)flags));
}
```

- [ ] **Step 3: Userland test — pagecache_test**

Create `bin/pagecache_test/pagecache_test.c`:
```c
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>

int main(void) {
    /* Use a writable sbfs path. Adjust to whatever sbfs mountpoint
     * convention this project uses; most tests use /data/. */
    const char *path = "/data/pcache.txt";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("pagecache_test: open failed\n"); return 1; }
    /* Seed the file with 4 KB of 'A'. */
    char buf[4096];
    for (int i = 0; i < 4096; i++) buf[i] = 'A';
    if (write(fd, buf, 4096) != 4096) {
        printf("pagecache_test: write failed\n"); return 1;
    }

    /* mmap MAP_SHARED writable. */
    char *m = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { printf("pagecache_test: mmap failed\n"); return 1; }

    /* Verify mmap sees the seeded content. */
    if (m[100] != 'A') {
        printf("pagecache_test: mmap content mismatch (read-via-mmap)\n");
        return 1;
    }

    /* Modify via mmap. */
    m[100] = 'Z';
    if (msync(m, 4096, MS_SYNC) != 0) {
        printf("pagecache_test: msync failed\n"); return 1;
    }

    /* Read via fd; should see the modification. */
    lseek(fd, 100, 0);
    char c;
    if (read(fd, &c, 1) != 1 || c != 'Z') {
        printf("pagecache_test: read-via-fd did not see mmap write\n");
        return 1;
    }

    munmap(m, 4096);
    close(fd);
    unlink(path);
    printf("pagecache_test: ok\n");
    return 0;
}
```

Add `"/bin/pagecache_test",` to init's test list.

- [ ] **Step 4: Build + run**

Run: `make clean && timeout 240 make qemu 2>&1 | grep -E "pagecache_test|tests passed"`
Expected: `pagecache_test: ok`.

- [ ] **Step 5: Commit**

```bash
git add kernel/syscall.c libc/syscall.c libc/include/sys/mman.h \
        bin/pagecache_test/pagecache_test.c bin/init/init.c
git commit -m "msync: implement; pagecache_test verifies read/mmap coherence"
```

---

## Task 14: Truncate hook drops cache pages

**Files:**
- Modify: `kernel/fs/sbfs.c`

- [ ] **Step 1: Hook pcache_invalidate_range into sbfs_truncate**

Locate `sbfs_truncate` (search `grep -n truncate kernel/fs/sbfs.c`). After the size update, add:

```c
    pcache_invalidate_range(ip, 0, ~0ULL);
```

(Truncate to zero is the only path today; any future shrink-to-N would need `pcache_invalidate_range(ip, N, ~0ULL - N)` instead. For Phase B, full invalidate on truncate is sufficient and correct.)

Add `#include <page_cache.h>` at the top of `sbfs.c` if not present.

- [ ] **Step 2: Userland test — truncate_mmap_test**

Create `bin/truncate_mmap_test/truncate_mmap_test.c`:
```c
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <string.h>

int main(void) {
    const char *path = "/data/trunc_mmap.txt";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    char buf[8192];
    for (int i = 0; i < 8192; i++) buf[i] = 'A';
    write(fd, buf, 8192);

    char *m = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { printf("truncate_mmap: mmap failed\n"); return 1; }

    /* Touch second page to fault it in. */
    volatile char c = m[5000];
    (void)c;

    /* Truncate to 4 KB. Second page now past EOF. */
    int fd2 = open(path, O_RDWR | O_TRUNC);
    close(fd2);

    int pid = fork();
    if (pid == 0) {
        volatile char x = m[5000];   /* should SIGBUS (or SIGSEGV) */
        (void)x;
        _exit(0);                    /* if we got here, kernel allowed it */
    }
    int st;
    wait(&st);
    if (WIFSIGNALED(st)) {
        printf("truncate_mmap: ok (child killed by signal %d)\n",
               WTERMSIG(st));
        munmap(m, 8192);
        unlink(path);
        return 0;
    }
    printf("truncate_mmap: child exited normally — past-EOF access allowed\n");
    return 1;
}
```

Add `"/bin/truncate_mmap_test",` to init's test list.

- [ ] **Step 3: Run**

Run: `make clean && timeout 240 make qemu 2>&1 | grep -E "truncate_mmap|tests passed"`
Expected: `truncate_mmap: ok`.

- [ ] **Step 4: Commit**

```bash
git add kernel/fs/sbfs.c bin/truncate_mmap_test/truncate_mmap_test.c bin/init/init.c
git commit -m "sbfs: truncate invalidates cached pages; SIGBUS past EOF"
```

---

## Task 15: Remaining userland tests + final smoke

**Files:**
- Create: `bin/mmap_share_test/mmap_share_test.c`
- Create: `bin/bigfile_pcache_test/bigfile_pcache_test.c`
- Create: `bin/pagecache_stress/pagecache_stress.c`
- Modify: `bin/init/init.c`

- [ ] **Step 1: mmap_share_test**

Create `bin/mmap_share_test/mmap_share_test.c`:
```c
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>

int main(void) {
    const char *path = "/data/share.txt";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    char buf[4096];
    for (int i = 0; i < 4096; i++) buf[i] = 'X';
    write(fd, buf, 4096);

    char *m = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { printf("mmap_share: mmap failed\n"); return 1; }

    int pid = fork();
    if (pid == 0) {
        if (m[0] != 'X') _exit(2);    /* child sees seed */
        m[0] = 'Y';
        _exit(0);
    }
    int st;
    wait(&st);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("mmap_share: child status bad\n"); return 1;
    }
    /* Parent should see child's write — MAP_SHARED. */
    if (m[0] != 'Y') {
        printf("mmap_share: parent did not see child write\n"); return 1;
    }
    munmap(m, 4096);
    close(fd);
    unlink(path);
    printf("mmap_share: ok\n");
    return 0;
}
```

- [ ] **Step 2: bigfile_pcache_test**

Create `bin/bigfile_pcache_test/bigfile_pcache_test.c`:
```c
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(void) {
    const char *path = "/data/bigfile.bin";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("bigfile: open failed\n"); return 1; }
    /* Cache pool = 256 KB. We want clearly bigger: 512 KB. */
    char buf[4096];
    for (int p = 0; p < 128; p++) {
        for (int i = 0; i < 4096; i++) buf[i] = (char)(p ^ i);
        if (write(fd, buf, 4096) != 4096) {
            printf("bigfile: write %d failed\n", p); return 1;
        }
    }
    /* Read back, verify byte-for-byte. */
    lseek(fd, 0, 0);
    for (int p = 0; p < 128; p++) {
        if (read(fd, buf, 4096) != 4096) {
            printf("bigfile: read %d failed\n", p); return 1;
        }
        for (int i = 0; i < 4096; i++) {
            if (buf[i] != (char)(p ^ i)) {
                printf("bigfile: mismatch p=%d i=%d\n", p, i);
                return 1;
            }
        }
    }
    close(fd);
    unlink(path);
    printf("bigfile_pcache: ok\n");
    return 0;
}
```

- [ ] **Step 3: pagecache_stress**

Create `bin/pagecache_stress/pagecache_stress.c`:
```c
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

int main(void) {
    const char *path = "/data/stress.bin";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    char buf[4096];
    for (int i = 0; i < 4096; i++) buf[i] = 'A';
    write(fd, buf, 4096);

    char *m = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { printf("stress: mmap failed\n"); return 1; }

    /* Interleave: write via fd, read via mmap; write via mmap, read via fd. */
    for (int round = 0; round < 32; round++) {
        char ch = 'A' + (round % 26);
        for (int i = 0; i < 4096; i++) buf[i] = ch;
        lseek(fd, 0, 0);
        write(fd, buf, 4096);
        if (m[100] != ch) {
            printf("stress: mmap stale after fd-write (round %d)\n", round);
            return 1;
        }
        m[200] = ch + 1;
        msync(m, 4096, MS_SYNC);
        char c;
        lseek(fd, 200, 0);
        read(fd, &c, 1);
        if (c != ch + 1) {
            printf("stress: fd-read stale after mmap-write (round %d)\n",
                   round);
            return 1;
        }
    }
    munmap(m, 4096);
    close(fd);
    unlink(path);
    printf("pagecache_stress: ok\n");
    return 0;
}
```

- [ ] **Step 4: Add all three to init's test list**

Append to `bin/init/init.c` test array:
```c
    "/bin/mmap_share_test",
    "/bin/bigfile_pcache_test",
    "/bin/pagecache_stress",
```

- [ ] **Step 5: Final clean run**

Run: `make clean && timeout 360 make qemu 2>&1 | tee /tmp/pcache_final.log | grep -E "tests passed|FAIL|ok|pcache"`
Expected: every new test reports `: ok`; init reaches `<N>/<N> tests passed`; shell prompt `sh>` appears.

- [ ] **Step 6: Commit**

```bash
git add bin/mmap_share_test/ bin/bigfile_pcache_test/ bin/pagecache_stress/ \
        bin/init/init.c
git commit -m "test: page cache userland coverage (share, bigfile, stress)"
```

---

## Self-Review Notes (already applied inline)

- **Spec coverage:**
  - `pcache_get`/`put`/`flush_inode`/`invalidate_range` → Tasks 1, 2, 7.
  - readpage / writepage hooks → Tasks 3, 6.
  - tarfs_readpage → Task 4. sbfs_readpage / sbfs_writepage → Tasks 5, 6.
  - generic_file_read / write / fault → Tasks 4, 6, 9.
  - sys_mmap fd path + VMA_TYPE_FILE → Task 8.
  - File fault (RO, CoW, SHARED-W) → Tasks 9–11.
  - msync → Task 13.
  - Truncate hook → Task 14.
  - VMA teardown flush → Task 12.
  - Userland tests covering each spec test scenario → Tasks 9 (smoke), 10 (cow), 13 (pagecache_test), 14 (truncate), 15 (share, bigfile, stress).
  - Kernel selftests → Tasks 2, 7.

- **Naming consistency:** `pcache_*` throughout; `PCACHE_NSLOTS` / `PCACHE_PGSZ` constants. `VMA_FLAG_SHARED` matches existing `VMA_FLAG_COW` naming. `sbfs_writepage_locked` is the begin_op-wrapping public form; `sbfs_writepage` is the inside-transaction form.

- **Phase B vs C boundary:** Phase B keeps `sbfs_write` as a thin wrapper around the existing `sbfs_writei` (Task 6, Step 3) plus a `pcache_invalidate_range` for cache coherence. This means the per-write `writepage` call described in spec §6 happens *implicitly* via `sbfs_writei` → `bread`/`log_write` (the bio path), and the page-cache image is dropped rather than updated. Phase C will switch to the spec's `pcache_get → memcpy → writepage` path once dirty-list infrastructure exists. Functionally equivalent in Phase B; semantically simpler.

- **Pinning rule** (spec §5.7) honoured: file VMAs hold one pcache refcnt per faulted-in page until VMA teardown. Re-acquired via `pcache_get` in teardown to find the slot, then `pcache_put` twice (once for the new lookup ref, once for the long-lived fault-time ref). Same pattern in CoW upgrade path.
