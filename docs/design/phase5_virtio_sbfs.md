# Phase 5 — VirtIO-blk + sbfs v1 + read-write `/data`

**Status:** design ready, not yet implemented
**Depends on:** Phase 3 (processes, syscalls), Phase 4 (VFS, fd table, tarfs read-only)
**Enables:** Phase 6 (shell can persist history, create files), Phase 7 (mmap a real file), Phase 9 (real disk-backed leak tests)

---

## 1. Goal

Give the kernel a real block device and a simple writable filesystem mounted at `/data`, so user programs can `open(..., O_CREAT|O_WRONLY)`, `write`, `close`, reboot the machine, and read the data back. Tarfs stays read-only at `/` (it is the immutable boot image); everything mutable lives under `/data`.

The filesystem (`sbfs v1`) is deliberately the smallest thing that still proves the contract: one superblock, a fixed inode table, a block bitmap, and **direct blocks only** (no indirect, no extents). File size cap is therefore `NDIRECT * BSIZE` ≈ 48 KiB, which is enough for every Phase 6 shell artifact (history files, small scripts) and every Phase 9 stress test without forcing us to write an indirect-block allocator we will throw away at Phase 10.

We pay the "real OS" tax exactly once here: the VirtIO driver, the buffer cache, the log (even if trivial), and the namei/writei machinery. Every later filesystem feature becomes an incremental change on top of this skeleton.

---

## 2. Preconditions

From earlier phases:
- QEMU `-drive file=sbfs.img,format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0,bus=virtio-mmio-bus.0` wired into the Makefile.
- PLIC initialized (Phase 3) so VirtIO's MMIO IRQ can route to hart 0.
- DT / hard-coded MMIO base for `virtio_mmio@10001000` (QEMU `virt`).
- VFS layer from Phase 4: `struct inode`, `struct file_ops`, `struct inode_ops`, `namei`, fd table, `mount_root`.
- Kernel heap (`kmalloc`/`kfree`) large enough to hold a buffer cache of ~32 entries × 512 B = 16 KiB plus the sbfs in-memory inode cache.
- `copyin`/`copyout` path cleanup from Phase 9 is **not** required; this phase still lives with the "pagetable walk + memmove" approach. The new filesystem code must not assume anything more.

Out of scope for this phase:
- Indirect blocks, symlinks, mtime/atime, permissions beyond a single mode word.
- Concurrent multi-writer correctness (single hart, IRQs-off critical sections suffice).
- Crash-consistency beyond "do not corrupt the FS if power is pulled between transactions". A tiny write-ahead log handles that; full `fsck` is explicitly deferred.

---

## 3. Concepts & data structures

### 3.1 On-disk layout (sbfs v1)

```
block 0        : boot block (unused, reserved for future mkfs signature)
block 1        : superblock
block 2        : log header + log blocks (LOGSZ blocks total)
block 2+LOGSZ  : inode table (NINODES inodes, packed)
...            : block bitmap (ceil(NBLOCKS/8/BSIZE) blocks)
...            : data blocks
```

Constants (tunable at mkfs time, baked into the superblock):

```c
#define BSIZE     512
#define NDIRECT   12          // 12 * 512 = 6 KiB per-file cap in v1
#define NINODES   256
#define LOGSZ     16          // 16 blocks of log, enough for 1 transaction
#define MAGIC     0x53425631  // "SBV1"
```

The 6 KiB per-file cap is intentionally small: it proves the direct-block path and forces us to test the "write past end of file returns short count" branch early. Phase 7+ will bump `NDIRECT` or add indirect blocks.

### 3.2 Superblock

```c
struct sb_superblock {
    uint32_t magic;        // MAGIC
    uint32_t size;         // total filesystem size in blocks
    uint32_t nblocks;      // number of data blocks
    uint32_t ninodes;      // NINODES
    uint32_t nlog;         // LOGSZ
    uint32_t logstart;     // block # of first log block
    uint32_t inodestart;   // block # of first inode block
    uint32_t bmapstart;    // block # of first bitmap block
};
```

Read once at mount, stashed in a static `struct sb_superblock sb;`. Never written after mkfs (v1 has no resize).

### 3.3 On-disk inode (`struct sb_dinode`)

```c
struct sb_dinode {
    uint16_t type;         // 0=free, 1=file, 2=dir
    uint16_t nlink;        // number of hard links
    uint32_t size;         // file size in bytes
    uint32_t addrs[NDIRECT];
};  // exactly 64 bytes → 8 inodes per 512 B block
```

Packing 8 per block means `NINODES=256` costs 32 blocks. Easy to walk, easy to verify in tests.

### 3.4 In-memory inode (`struct inode` extension)

Phase 4 already has a generic `struct inode`. For sbfs we add a `union i_private` field that points at:

```c
struct sb_inode_mem {
    uint32_t inum;
    int      valid;        // 1 after the dinode has been read in
    struct sb_dinode d;    // cached copy; written back via log
};
```

Access is serialized by a global `sbfs_lock` (spinlock / IRQs-off section). There is no per-inode lock yet because we are single-hart.

### 3.5 Directory entry (`struct sb_dirent`)

```c
#define DIRSIZ 14
struct sb_dirent {
    uint16_t inum;         // 0 = empty slot
    char     name[DIRSIZ]; // not NUL-terminated if full
};  // 16 bytes, 32 entries per 512 B block
```

Max 12 × 32 = 384 entries in a sbfs v1 directory. That is the first **limit test**: create 385 files in `/data` and verify `ENOSPC` (not a panic).

### 3.6 Buffer cache (`bio.c`)

LRU list of `NBUF = 32` blocks. Each buffer holds a `blockno`, a dirty flag, a reference count, and a pointer to 512 bytes of data. The interface:

```c
struct buf *bread(uint32_t dev, uint32_t blockno);  // cached or DMA-in
void        bwrite(struct buf *b);                  // mark dirty, log will flush
void        brelse(struct buf *b);                  // drop refcount
```

IRQs off while holding more than one buffer. No sleep-locks; everything is short.

### 3.7 Mini write-ahead log (`log.c`)

We need crash-consistency for the two "compound" operations in v1:
1. Allocating a block and writing it into an inode's `addrs[]`.
2. Allocating an inode and writing its directory entry.

The log is append-only, capped at `LOGSZ - 1` blocks of data + 1 header block. A transaction is:

```
begin_op();
bp = bread(...);  modify bp->data;  log_write(bp);
...
end_op();   // writes header, then replays into real locations, then zeros header
```

If we crash before the header is written → replay is a no-op. If we crash after → replay redoes the transaction. This is xv6's log, boiled down to the minimum.

**This phase deliberately keeps `begin_op()` a single global mutex.** There is exactly one in-flight transaction at a time. When we get concurrent processes doing filesystem work in Phase 6+, we either keep the global lock (fine for a toy OS) or extend to reference-counted transactions. That decision is punted (§9).

### 3.8 VirtIO-blk driver (`virtio_disk.c`)

- Single virtqueue, size 8.
- Request descriptors are a 3-entry chain: header (`struct virtio_blk_req_hdr`), data (512 B payload), status byte.
- Kernel thread / caller blocks via `proc_sleep` on a per-request wait channel until the IRQ handler wakes it.
- **Phase 5a ships polling mode first** (spin on `used_idx`) and **Phase 5b switches to IRQ + sleep/wakeup**. Reason: we want the buffer cache and sbfs code under test before we debug MMIO interrupt routing.
- `__sync_synchronize()` / explicit `fence rw,rw` around every shared-memory doorbell and every descriptor publish. Without a fence the compiler will happily reorder `used_idx` reads.

---

## 4. File-by-file changes

Split into four sub-PRs so each one is independently reviewable and testable.

### 4a — VirtIO-blk in polling mode + buffer cache

New:
- `kernel/virtio_disk.c`, `kernel/include/virtio.h`
  - `virtio_disk_init()`: probes MMIO, negotiates features (no `VIRTIO_BLK_F_RO`), sets up virtqueue.
  - `virtio_disk_rw(struct buf *b, int write)`: builds descriptor chain, kicks the queue, spins on `used_idx` (5a) or sleeps (5b).
- `kernel/bio.c`, `kernel/include/bio.h`
  - Static array of 32 `struct buf`, LRU doubly-linked list, `bread/bwrite/brelse/binit`.

Modified:
- `kernel/main.c` (or wherever kernel init runs) calls `binit()` then `virtio_disk_init()` before `sched_init()`.
- `Makefile` / build scripts: create `sbfs.img` as a zeroed 4 MiB file so QEMU has something to attach. `mkfs` will overwrite it.

### 4b — mkfs tool + sbfs read path

New:
- `tools/mkfs/mkfs.c` (hosted, compiled with host gcc): takes `sbfs.img` + a list of files, writes a valid sbfs v1 image with `/` as an empty directory (or with a few seeded files for bring-up tests).
- `kernel/sbfs.c`, `kernel/include/sbfs.h`
  - `sbfs_mount(int dev)`: reads superblock, verifies magic.
  - `sbfs_iget(uint32_t inum)`, `sbfs_iput`, `sbfs_ilock`, `sbfs_iunlock` — cached in-memory inode table of 64 entries.
  - `sbfs_readi(struct inode *, char *dst, uint32_t off, uint32_t n)`: walks `addrs[]`, `bread`s each block, copies out.
  - `sbfs_namei` (directory lookup, uses `readi` internally).
  - `struct file_ops sbfs_file_ops = { .read = ..., .write = EROFS_for_now };`
  - `struct inode_ops sbfs_inode_ops = { .lookup = sbfs_dirlookup, ... };`

Modified:
- `kernel/vfs.c`: mount table gains an entry `{ "/data", &sbfs_root_inode }`. `namei` checks longest-prefix match before falling through to the tarfs root.

At the end of 4b we can `cat /data/hello.txt` but cannot write.

### 4c — Block bitmap + log + write path

New:
- `kernel/log.c`, `kernel/include/log.h`
  - `begin_op()`, `end_op()`, `log_write(struct buf *)`.
  - `install_trans()` (commit side), `recover_from_log()` called from `sbfs_mount`.
- `kernel/sbfs_alloc.c` (or in `sbfs.c`): `balloc`, `bfree` using the on-disk bitmap.
- `sbfs_writei`, `sbfs_itrunc` (truncation on unlink / O_TRUNC).

Modified:
- `sbfs_file_ops.write` now calls `sbfs_writei` inside `begin_op/end_op`.
- `sys_open` honours `O_CREAT` by calling a new `sbfs_create(parent, name, type)` which allocates a free dinode, writes a directory entry, and returns an `inode *`.

### 4d — Directory ops, unlink, mkdir

New syscalls in `kernel/sys_file.c`:
- `sys_mkdir(const char *path, mode_t mode)` — ignores mode, allocates a directory inode, seeds `.` and `..`.
- `sys_unlink(const char *path)` — decrements nlink, frees inode + blocks when nlink hits 0 and no fd refers to it. (We keep nlink=1 for regular files in v1 because `sys_link` is **not** implemented; the field exists so Phase 10 can add it without an on-disk format change.)

The unlink-while-open case is the classic Unix contract: the directory entry vanishes immediately, the inode's blocks are freed only when the last fd closes. In-memory inode cache holds a ref that decrements on `fileclose`.

---

## 5. Key flows

### 5.1 `open("/data/log.txt", O_CREAT|O_WRONLY, 0)`

1. `sys_open` → `namei_parent("/data/log.txt", name_out)` returns the `/data` dinode and the leaf `"log.txt"`.
2. `sbfs_dirlookup("log.txt")` returns 0 (not found). Because `O_CREAT` is set, we call `sbfs_create(dir, "log.txt", T_FILE)`:
   - `begin_op()`
   - `balloc_inode()` walks the inode table looking for `type == 0`.
   - Write a new dinode with `type=1`, `nlink=1`, `size=0`, `addrs={0}` via `log_write`.
   - Find a free slot in the directory's data blocks (may require `balloc` of a new data block if the directory is full).
   - Write the directory entry via `log_write`.
   - `end_op()` → log commit replays both buffers to their real homes.
3. Return an `inode *` → `filealloc` → fd.

If we crash between `balloc_inode` and writing the directory entry: on mount, the log header is either absent (transaction was never committed, inode stays free, safe) or present (replay writes both, safe). There is no torn state.

### 5.2 `write(fd, buf, 5000)` on a file at offset 10000

File's current size is 4000. The write spans offsets 10000–14999, so it leaves a hole from 4000–9999. sbfs v1 **does not support holes**: `writei` refuses to extend `size` without allocating blocks for every byte in between. Implementation:

- Compute `end = min(off + n, NDIRECT * BSIZE)`. If `off > NDIRECT*BSIZE` → return `-EFBIG`.
- For each block index `bn` in `[off/BSIZE, end/BSIZE]`:
  - If `addrs[bn] == 0`: `balloc` a new data block, `log_write` the inode with the new `addrs[]`.
  - `bread` that data block, overwrite the relevant bytes, `log_write` it.
- If `end > size`, update `size` and `log_write` the inode.
- Return number of bytes actually written (may be less than `n` if we hit the 6 KiB cap).

The short-write case is the entire reason for the 6 KiB cap: every Phase 9 stress test exercises it.

### 5.3 Mount + log recovery on boot

1. `virtio_disk_init()` → device ready.
2. `binit()` → buffer cache warm.
3. `sbfs_mount(dev=0)`:
   - `bread(1)` → superblock, verify magic.
   - `recover_from_log()`: read log header; if `n > 0`, replay each logged block to its real destination, then zero the header.
   - Build the root inode of `/data`.
4. `vfs_mount("/data", &sbfs_root);`

If the user reboots between `end_op`'s "write header" and "install transactions" steps, recovery replays the transaction. If they reboot before the header write, recovery sees `n=0` and skips.

---

## 6. Syscall ABI & error codes

| Syscall  | Num (TBD) | Args                                        | Returns                                |
|----------|-----------|---------------------------------------------|----------------------------------------|
| `mkdir`  | 50        | `const char *path, mode_t mode`             | 0, `-ENOENT`, `-EEXIST`, `-ENOSPC`, `-ENOTDIR`, `-EFAULT` |
| `unlink` | 51        | `const char *path`                          | 0, `-ENOENT`, `-EISDIR`, `-EFAULT`     |
| `sync`   | 52        | none                                        | 0 (no-op in v1; log commits are synchronous) |

Existing syscalls pick up new error returns:
- `open(O_CREAT)`: `-ENOSPC` (out of inodes or blocks), `-EROFS` (attempt on tarfs root).
- `write`: `-EFBIG` (exceeds 6 KiB cap), short counts at the cap.

We deliberately **do not** add `rename`, `link`, `symlink`, `truncate`, `ftruncate` in this phase. They are one-line stubs returning `-ENOSYS`.

---

## 7. Test plan

The test plan is split into **unit**, **end-to-end**, and **limit/stress** per the explicit requirement. "Unit" means exercised by `kernel/selftest.c` from inside the kernel without needing a full user process; "e2e" means driven by a user-space test binary through real syscalls; "limit/stress" means it deliberately hits a boundary that a naive implementation will get wrong.

### 7.1 Unit tests (`kernel/selftest.c` additions)

| ID   | Test                                         | What it proves |
|------|----------------------------------------------|----------------|
| U-1  | `bread` then `brelse` the same block 100×    | Buffer cache refcount returns to 0, no leaks |
| U-2  | Fill cache (32 distinct blocks), read a 33rd | LRU eviction picks the oldest clean buffer |
| U-3  | `bwrite` a dirty block, re-`bread` it        | Buffer cache returns the dirty copy, not a fresh DMA |
| U-4  | `balloc` until exhausted, check `bfree` restores | Bitmap accounting is symmetric |
| U-5  | `balloc_inode` until exhausted (256 inodes)  | `type==0` scan finds every slot |
| U-6  | `log_write` 10 blocks, crash simulation (skip `install_trans`), `recover_from_log` | Replay reaches the real blocks |
| U-7  | `sbfs_writei` at offset `NDIRECT*BSIZE - 10` with `n=20` | Short count = 10, no overflow, no corrupt neighbor block |
| U-8  | `sbfs_dirlookup` on directory with 384 entries, last entry | Linear scan walks every direct block |

### 7.2 End-to-end tests (user-space binaries under `bin/`)

| ID   | Binary             | Scenario                                                                                  |
|------|--------------------|-------------------------------------------------------------------------------------------|
| E-1  | `sbfs_basic_test`  | `open(O_CREAT)`, `write "hello"`, `close`, `open(O_RDONLY)`, `read`, compare              |
| E-2  | `sbfs_persist_test`| Write a file; selftest harness triggers `qemu reboot`; on next boot a second binary reads it back. (Requires a second QEMU invocation — wire into `make test` as a two-stage run.) |
| E-3  | `sbfs_mkdir_test`  | `mkdir /data/a`, `mkdir /data/a/b`, `open("/data/a/b/f", O_CREAT)`, fstat → `S_ISREG`     |
| E-4  | `sbfs_unlink_test` | Create file, `open` it, `unlink`, confirm the open fd still reads successfully, close, re-`open` → `ENOENT` |
| E-5  | `sbfs_tarfs_rofs`  | `open("/etc/rc", O_WRONLY)` → either open fails or `write` returns `-EROFS` (same contract as Phase 4 path_test) |
| E-6  | `sbfs_offset_test` | `write "abc"`, seek to 0, `write "X"`, read back → `Xbc` (in-place modify inside a block) |
| E-7  | `sbfs_mount_edge`  | `open("/data")` returns dir fd, `getdents` lists files created by E-1/E-3                 |

### 7.3 Limit / stress tests (explicitly hunt for bugs that hide at small N)

These tests are the reason the defaults are small. Every one of them is a line where a real OS would run for hours and a toy OS quietly corrupts data.

| ID    | Binary                   | Boundary                                                                      | Failure mode it catches |
|-------|--------------------------|-------------------------------------------------------------------------------|-------------------------|
| L-1   | `sbfs_fill_inodes`       | Create 256 files in `/data` (the NINODES cap), then a 257th                   | `balloc_inode` returns error, not panic; `open` returns `-ENOSPC` |
| L-2   | `sbfs_fill_blocks`       | Write data until every data block is allocated, then try one more `O_CREAT`   | `balloc` returns error, `sbfs_create` rolls back the inode allocation (no ghost inodes) |
| L-3   | `sbfs_dir_fill`          | Create 384 files in one directory, then a 385th                               | Directory grows across all direct blocks; 385th returns `-ENOSPC`, directory still intact |
| L-4   | `sbfs_max_file`          | `write` 6 KiB, then attempt to write 1 more byte                              | Returns 0 or `-EFBIG`, size stays at cap |
| L-5   | `sbfs_many_opens`        | Spawn 16 processes, each `open`s the same file, reads, closes                 | Shared inode refcount survives concurrent `iget`/`iput` |
| L-6   | `sbfs_log_overflow`      | Single transaction that dirties `LOGSZ+1` blocks                              | `log_write` must detect and `panic("log overflow")` rather than silently corrupting — this is the one place panic is correct |
| L-7   | `sbfs_reboot_mid_txn`    | Drive a write, simulate crash *after* header write but *before* install       | Next boot recovers exactly; data is visible, buffer cache is cold |
| L-8   | `sbfs_reboot_before_hdr` | Simulate crash *before* header write                                          | Next boot sees `n=0`, transaction is gone, no partial state |
| L-9   | `sbfs_buf_thrash`        | Sequentially `bread` 1000 distinct blocks with no reuse                       | LRU never returns a buffer whose refcount > 0; no `bread` panics |
| L-10  | `sbfs_unlink_open_race`  | Proc A keeps fd open, proc B unlinks + recreates same name, proc A still reads old contents | Inode's on-disk blocks aren't reused until A closes |

L-7 and L-8 need a new selftest primitive: the ability to `panic_without_log_install()` in a test build. Hide it behind `#ifdef SBFS_CRASH_TESTS` so the production binary never has it.

### 7.4 Test gate

Phase 5 is not "done" until:
- Every U-*, E-*, and L-* test passes.
- `sbfs.img` survives 100 back-to-back `make qemu` runs with `sbfs_basic_test` rewriting the same file, with zero magic-number corruption.
- Running **all** Phase 3 + Phase 4 + Phase 5 tests together (≈25 processes) completes within a 60-second QEMU timeout.

---

## 8. Gotchas

- **DMA coherence on QEMU is lax; real hardware is not.** Every `virtio_disk_rw` must issue a full fence before kicking the queue and after reading `used_idx`. Missing fences will work in QEMU and blow up on anything real. Do not skip this even if the tests pass.
- **Buffer cache aliasing.** If two callers hold the same `blockno` buffer simultaneously (allowed, refcount > 1), dirty bits must be tracked on the buffer, not on the caller's copy. Bug to watch for: caller A dirties a buffer, caller B `brelse`s cleanly, the dirty bit accidentally cleared.
- **Log `begin_op` inside `bread` is a deadlock.** `bread` can sleep (5b) waiting for DMA. If the log reserves a slot and then sleeps on `bread`, another process doing `begin_op` will deadlock waiting for the slot. Reserve log space *after* all `bread`s in a transaction.
- **Directory linearity.** `sbfs_dirlookup` walks entries in order. An unlink leaves an `inum==0` hole. `sbfs_dirlink` must scan for holes before extending the directory. Bug: always extending leads to a monotonically growing directory that never shrinks.
- **Inode cache vs on-disk type.** A `sbfs_iget` that hits a cached but stale in-memory inode (because another process wrote via log) must invalidate. Simplest answer: `sbfs_iput` discards the cache entry when its refcount hits 0, and `sbfs_iget` always re-reads the dinode from the log-installed buffer. Slower, but correct.
- **mkfs host-vs-target endianness.** Both sides are little-endian (RV64 and x86-64 dev hosts), so we get away with `memcpy`-of-struct. Document this; the day someone tries to cross-compile from a big-endian host this will break.
- **The VirtIO legacy vs modern split.** QEMU's `virt` board exposes the legacy MMIO interface unless `-global virtio-mmio.force-legacy=false`. Pick one and stick with it; the initialization sequences are incompatible.
- **`sync` as a no-op lies in the future.** It is a no-op today only because every `end_op` is synchronous. If Phase 7+ ever buffers transactions, `sync` must actually flush.
- **`sbfs_create` is not atomic across inode + dirent.** That is what the log is for. Do not try to "optimize" by skipping the log for the initial create — you will crash-corrupt the FS on day one.
- **Short writes aren't errors.** A write that returns 10 when the caller asked for 20 is *correct* when the file hit the size cap. User code (and libc) must handle this. Add `sbfs_max_file` (L-4) to catch any libc wrapper that loops until `n` bytes are written and never notices the cap.

---

## 9. Open questions / decisions punted

1. **Concurrent transactions.** Today every `begin_op` is globally serialized. Is a reference-counted outstanding-op design (xv6-style) worth doing now, or when Phase 6's shell pipelines actually produce concurrent writes? **Lean:** punt to Phase 7 unless the Phase 6 tests visibly deadlock.
2. **Block size.** 512 B matches the VirtIO sector and keeps math trivial. 4 KiB would match pages and let us `mmap` sensibly in Phase 7. **Lean:** stay on 512 in v1, cut over to 4 KiB at the same time we add indirect blocks.
3. **IRQ vs polling.** Ship 5a polling, 5b IRQ. Is polling good enough permanently for a teaching OS? Yes for single-process workloads, no once Phase 6 runs pipelines that block on disk. Decide after benchmarking.
4. **Inode cache size.** 64 entries is a guess. Phase 9's leak test will tell us if eviction churn matters.
5. **`T_DEV` for `/dev/console`.** Phase 4 puts `/dev/console` at a tarfs path. Should sbfs v1 learn about device inodes too, or does `/dev` stay exclusively on tarfs? **Lean:** stay on tarfs; sbfs knows only files and directories.
6. **mkfs seed content.** Should mkfs populate `/data` with any initial files (README, version string) or leave it empty? **Lean:** empty, with a single `.sbv1` marker file the persist test can look for.

---

## 10. Exit criteria

- [ ] `virtio_disk_init()` succeeds at boot, IRQ or poll mode selected at compile time.
- [ ] `binit()` + LRU eviction verified by U-1, U-2, U-3.
- [ ] `tools/mkfs/mkfs` produces a valid image that the kernel mounts without warning.
- [ ] `sbfs_mount` replays the log on every boot, even when empty.
- [ ] `open(O_CREAT)`, `write`, `read`, `close`, `mkdir`, `unlink` all return correct values and correct negative errnos at the syscall boundary.
- [ ] Every U-*, E-*, L-* test in §7 passes, and the full Phase 3+4+5 test suite finishes within the QEMU timeout.
- [ ] `sbfs_persist_test` survives a QEMU reboot with data intact.
- [ ] No test leaks inodes or blocks. Verify by reading the bitmap + inode table at end of each test and asserting counts match the initial mkfs state.
- [ ] `kernel/selftest.c` total count increases by at least 40 assertions vs end of Phase 4.
- [ ] `docs/design/README.md` updated to mark Phase 5 "shipped" rather than "design ready".
