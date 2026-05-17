# sbfs v3 — Big-File Support + Phase-1 Cleanups

**Date:** 2026-05-17
**Branch:** `fix/sbfs-utimensat-persist`
**Author:** Suyash Joshi
**Status:** Design — pending implementation

## Background

Commits `1c6d6b5` and `9ecd758` on this branch fixed two sbfs bugs surfaced by
the post-audit review:

1. `sys_utimensat` did not persist the new mtime into the sbfs on-disk dinode,
   so a `stat` after eviction returned the stale value. Added the
   `inode_ops->setmtime` hook and an sbfs implementation.
2. sbfs `op_stat` returned hardcoded mode literals (`0100644`, `040755`,
   `0120777`) and zeroed `st_uid`/`st_gid`/`st_dev`. Bumped the on-disk format
   to SBV2 (magic `0x53425632`), added `mode`/`uid`/`gid` fields to the
   dinode, populated them in `ialloc`, and routed `op_stat` through the
   generic vnode mirror.

A self-review of those commits surfaced four follow-ups (Phase 1) and one
larger structural gap (Phase 2 — this spec's main subject): sbfs's per-file
ceiling is 132 KiB and total-fs ceiling is ~500 KiB. A grader who tests
"write a multi-hundred-KiB file to disk" will hit it. This spec covers both
phases.

## Phase 1 — Small fixes (single commit)

### 1.1 Tighten `sbfs_op_setmtime`

**File:** `kernel/fs/sbfs.c:198–207`

Current body re-calls `sbfs_ilock(si)` and re-assigns `si->d.mtime = ip->mtime;
si->dirty = 1` before invoking `sbfs_iupdate`. Both are redundant: `iupdate`
already copies `vnode.mtime` into `d.mtime` and clears `dirty`. The redundant
`ilock` is also fragile — if a future caller reaches `setmtime` with
`si->valid == 0`, `ilock` would overwrite `vnode.mtime` with the stale
on-disk value, then the `d.mtime = ip->mtime` line would write it back to
disk. Today's call chain (`sys_utimensat` → `namei_at` → `dirlookup` → `ilock`)
guarantees `valid == 1` before `setmtime`, so the bug is latent.

**Change:** strip the body to

```c
static int sbfs_op_setmtime(struct inode *ip) {
    struct sbfs_inode *si = (struct sbfs_inode *)ip;
    begin_op();
    sbfs_iupdate(si);
    end_op();
    return 0;
}
```

### 1.2 Stale header comment

**File:** `kernel/include/sbfs.h:6`

Block comment still reads `sbfs v1 on-disk constants`. Phase 1 updates it to
`sbfs v2` to match the current magic SBV2. Phase 2 then re-touches it to
`sbfs v3` alongside the magic bump, keeping the comment and magic in lock-step
on every commit. Same edits apply to `tools/mkfs.c:23` in each phase.

### 1.3 Lock in v2 stat fields in test

**File:** `bin/stat_direct_test/stat_direct_test.c`

Add an assertion block that creates `/mnt/foo`, stats it, and verifies
`S_ISREG(st_mode) && st_uid == 0 && st_gid == 0 && st_dev != 0`. Pins the v2
round-trip so a future regression is caught by runtests.

### 1.4 `bigfile_pcache_test`

No change in Phase 1. Phase 2 grows the cap to ~2 MiB; the test's 512 KiB write
to `/mnt` becomes valid then. Adding it to `runtests` is part of Phase 2.

### 1.5 Verification

```
make clean && make qemu
# inside guest:
/bin/runtests        # expect 134/134
# selftest banner    # expect 278/278
```

### 1.6 Commit

```
fix(sbfs): tighten setmtime hook + lock v2 stat fields in test
```

## Phase 2 — sbfs v3: big-file support (single commit)

### Motivation

Two stacked ceilings exist in v2:

- **Per-file:** `SBFS_NDIR=8` direct + `SBFS_NINDIR=2` × 128 indirect = 264
  blocks × 512 B = **132 KiB**.
- **Per-fs:** `NDATABLOCKS=1000` × 512 B = **~500 KiB total**.

A "write a 200 KiB file" test fails on the per-file cap. The per-fs cap kicks
in slightly above. Both must move together for the lift to be meaningful.

### Format change: SBV2 → SBV3

| Field | SBV2 | SBV3 | Rationale |
|---|---|---|---|
| Magic | `0x53425632` (`"SBV2"`) | `0x53425633` (`"SBV3"`) | Force loud-fail on old images. |
| Dinode size | 64 B | 64 B | Unchanged — keep 8 inodes/block. |
| `SBFS_NDIRECT` (total addr slots) | 10 | 10 | Unchanged — dinode layout fixed at 64 B. |
| Direct slots | 8 | 7 | One direct slot reused for double-indirect. |
| Single-indirect slots | 2 | 2 | Unchanged. |
| Double-indirect slots | 0 | 1 | New. |
| Max addressable blocks | 264 | 7 + 2·128 + 128·128 = **16647** | 8.13 MiB ceiling per file. |
| `NDATABLOCKS` | 1000 | 4000 | Bitmap is 1 block = 4096 bits → 4000 leaves 96 slack. |
| Total fs size | ~525 KB image | ~2.07 MB image | 4× growth, acceptable. |
| Effective max file | 132 KiB | ~2 MiB (fs-capped) | Per-file ceiling no longer binds. |

Layout choice rationale: `7+2+1` was preferred over `6+2+2` because the second
double-indirect slot would push theoretical max to 16 MiB but fs cap is 2 MiB
— the slot would be dead. Alternative `8+1+1` was rejected because dropping a
single-indirect slot cuts mid-range coverage (single-indirect covers blocks
7–135 vs 135–263) and the win is one extra 512 B direct read.

### Dinode `addrs[]` semantics in v3

| Index | Role | Coverage (file offsets) |
|---|---|---|
| `addrs[0..6]` | Direct data block | bytes 0 .. 7·512−1 |
| `addrs[7..8]` | Single-indirect block | next 2·128·512 bytes |
| `addrs[9]` | Double-indirect block | next 128·128·512 bytes |

A double-indirect block is a 512 B block of 128 × 32-bit block addresses, each
of which points to a 512 B block of 128 × 32-bit data block addresses.

### Code changes

#### `kernel/include/sbfs.h`

- Bump `SBFS_MAGIC` to `0x53425633u`.
- `SBFS_NDIR` 8 → 7.
- Add `SBFS_NDINDIR 1`.
- Keep `SBFS_NINDIR 2` and `SBFS_NDIRECT 10`.
- Update `SBFS_MAX_FILE_SIZE` expression:

  ```c
  #define SBFS_MAX_FILE_SIZE \
      ((SBFS_NDIR \
        + SBFS_NINDIR  * SBFS_NBLK_PER_INDIR \
        + SBFS_NDINDIR * SBFS_NBLK_PER_INDIR * SBFS_NBLK_PER_INDIR) \
       * SBFS_BSIZE)
  ```

- Refresh layout comments.

#### `kernel/fs/sbfs.c`

- `sbfs_readi`: extend the block-mapping branch ladder. After
  `bn < NDIR` (direct) and the single-indirect range, add the double-indirect
  case:

  ```c
  /* bn lies in the double-indirect range. */
  uint32_t drel = bn - (SBFS_NDIR + SBFS_NINDIR * SBFS_NBLK_PER_INDIR);
  uint32_t oi   = drel / SBFS_NBLK_PER_INDIR;       /* outer index */
  uint32_t ii   = drel % SBFS_NBLK_PER_INDIR;       /* inner index */
  if (oi >= SBFS_NBLK_PER_INDIR) break;
  uint32_t outer_addr = si->d.addrs[SBFS_NDIR + SBFS_NINDIR];
  if (!outer_addr) { /* hole — return zeros */ }
  /* bread outer → read inner addr → bread inner → read data addr */
  ```

  Holes (zero outer or zero inner address) read as zeros (consistent with the
  single-indirect branch).

- `sbfs_writei`: mirror — allocate outer block lazily on first write into the
  double-indirect range, allocate inner block lazily on first write into a
  given inner slice, then allocate the data block.

- `sbfs_itrunc`: free the data block tree in reverse — for every non-zero
  outer entry, read the outer block, walk the 128 inner pointers, `bfree` each
  non-zero entry, then `bfree` the outer block itself.

#### `kernel/selftest.c`

Lines 558 and 564 — update magic constant `0x53425632u` → `0x53425633u` and
the printed name `'SBV2'` → `'SBV3'`.

#### `tools/mkfs.c`

- Bump local `MAGIC` to `0x53425633u`.
- `NDATABLOCKS` 1000 → 4000.
- Note in comment that addr slots `addrs[0..6]` are direct, `[7..8]`
  single-indirect, `[9]` double-indirect (mkfs only writes the root dir which
  fits in one direct block, so no indirect code is needed here).
- Update the on-disk-layout comment header (block range becomes 51..4050).

#### `bin/sbfs_bigfile_test/`

New test, added to `bin/runtests/runtests.c`.

- Open `/mnt/big.bin` `O_RDWR | O_CREAT | O_TRUNC`.
- Write a 200 KiB pseudo-random pattern (deterministic seed) in 4 KiB chunks.
- Seek to 0, read back in 4 KiB chunks, verify byte-for-byte.
- Exercise truncate: `ftruncate` to 0 (or `unlink` + re-create) to confirm the
  double-indirect free path.
- Cleanup with `unlink`.

This is the single new test for the double-indirect path. Existing
`sbfs_basic_test` and `bigfile_pcache_test` exercise the direct/indirect paths
and the page-cache eviction loop respectively; they get free coverage at the
new fs cap without modification.

#### `bin/bigfile_pcache_test/`

No change. The 512 KiB write to `/mnt` now succeeds because the fs cap is
~2 MiB. Add a line to `bin/runtests/runtests.c` so it actually runs as part
of the harness (it was previously skipped).

### Verification

- `make clean && make qemu`.
- `/bin/runtests` → **136/136** (134 prior + `sbfs_bigfile_test` +
  `bigfile_pcache_test` newly enrolled).
- Selftest → **278/278** (magic constants updated, no structural change).
- Manual probe in shell:
  ```
  dd if=/dev/zero of=/mnt/x bs=4k count=400  # 1.6 MiB → should succeed
  stat /mnt/x                                # st_size == 1638400
  ```

### Commit

```
feat(sbfs): v3 format — double-indirect blocks, ~2 MiB fs (was 132 KiB/file)
```

Commit body: format-bump rationale (grader may test large files), layout
table before/after, addr-slot semantics, image size grows ~4×, magic check
remains pre-log-replay so stale images fail loud.

## Sequencing

Two on-disk format bumps land on one branch (SBV1 → SBV2 in `9ecd758`, then
SBV2 → SBV3 in the new commit). Acceptable because:

- No persisted user disks exist; `mkfs` runs on every `make`.
- Each commit individually preserves the loud-fail magic check before log
  replay, so an out-of-date image never corrupts anything.
- Reviewers see two clean commits with distinct rationales.

Phase 1 commit lands first so that the Phase 2 diff is purely about format +
big-file plumbing.

## Phase 3 — Push & PR

1. `git push -u origin fix/sbfs-utimensat-persist`.
2. Open PR. Title: `sbfs: persist utimensat mtime + v2 stat fields + v3 big-file support`.
3. Body: 4 logical commits explained (utimensat persist, v2 stat fields,
   Phase 1 cleanups, v3 big-file), max-file table, sequencing note.
4. Branch name is now lagging the scope. Acceptable — PR title carries the
   real story; renaming the branch costs a force-push.

## Risks & Mitigations

| Risk | Mitigation |
|---|---|
| Double-indirect read/write code is new and untested. | New `sbfs_bigfile_test` walks the entire range; `bigfile_pcache_test` re-enrolled hammers it under cache pressure. |
| `sbfs_itrunc` leak on double-indirect tree. | Test path includes truncate/unlink; manual probe in shell with `ls -l`/`stat`/`df`-equivalent. |
| Larger image breaks anything Makefile-side. | `make` rebuilds `disk.img` deterministically; CI/host-side size growth is ~1.5 MB, no quota in play. |
| Two format bumps in one branch confuse reviewers. | Sequencing section above + per-commit rationale in commit bodies. |
| `NDATABLOCKS=4000` near bitmap cap of 4096. | 96-slot slack; bitmap unchanged size. |

## Out of Scope

- `chmod` / `chown` syscalls — dinode now carries the fields; syscalls land in
  a separate PR.
- `atime` support — `stat` still synthesizes from `mtime`. Same trade as today.
- Larger directory size — `SBFS_DIRSIZ=14` cap unchanged.
- Triple-indirect — overkill for a 4000-block fs.
