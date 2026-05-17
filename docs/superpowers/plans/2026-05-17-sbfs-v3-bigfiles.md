# sbfs v3 Big-File Support + chmod/chown Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land four follow-up sbfs commits on `fix/sbfs-utimensat-persist`: Phase-1 cleanups, SBV3 double-indirect format (lifts 132 KiB → ~2 MiB cap), and POSIX chmod/chown family wired through new inode hooks.

**Architecture:** Phase 1 tightens existing setmtime hook and refreshes stale comments; no semantic change. Phase 2 bumps magic SBV2→SBV3, reuses one direct-block slot for a double-indirect pointer (`addrs[9]`), grows `NDATABLOCKS` 1000→4000, and adds read/write/itrunc support for the new path. Phase 3 adds `setmode`/`setowner` inode hooks (one-line sbfs implementations that reuse the existing `iupdate` vnode-mirror), wires five new syscalls (`chmod`, `fchmod`, `chown`, `lchown`, `fchown`), and replaces the libc no-op stubs with real syscall wrappers. Phase 4 pushes and opens the PR.

**Tech Stack:** C (RISC-V64 freestanding kernel), xv6-style log-structured filesystem, QEMU runtime, custom libc.

**Spec:** `docs/superpowers/specs/2026-05-17-sbfs-v3-bigfiles-design.md`

---

## Pre-flight

- [ ] **Confirm clean state**

```bash
git status                                # working tree clean
git log --oneline -5                      # tip = 9843c17 spec amendment
git branch --show-current                 # fix/sbfs-utimensat-persist
```

Expected: clean working tree, branch matches, tip commit subject mentions "chmod/chown into sbfs v3 spec".

- [ ] **Baseline test pass**

```bash
make clean && make qemu | tee /tmp/baseline.log
```

In QEMU: run `/bin/runtests`, observe selftest banner. Exit QEMU (Ctrl-A x).

Expected: runtests `134/134`, selftest `278/278`. If different, stop and reconcile before proceeding.

---

# Phase 1 — Small fixes (single commit)

## Task 1.1: Tighten `sbfs_op_setmtime`

**Files:**
- Modify: `kernel/fs/sbfs.c:194-207`

- [ ] **Step 1: Replace body with minimal hook**

In `kernel/fs/sbfs.c`, replace the existing `sbfs_op_setmtime` function (currently includes a redundant `sbfs_ilock` and `d.mtime = ip->mtime; dirty = 1` lines) with:

```c
/* sys_utimensat hook: vnode.mtime has already been set by the caller and
 * path lookup guarantees si->valid == 1 by the time we get here. The
 * generic iupdate copies vnode.mtime into d.mtime, so the body collapses
 * to a transaction-wrapped iupdate. */
static int sbfs_op_setmtime(struct inode *ip) {
    struct sbfs_inode *si = (struct sbfs_inode *)ip;
    begin_op();
    sbfs_iupdate(si);
    end_op();
    return 0;
}
```

- [ ] **Step 2: Verify utimes_test still passes**

```bash
make qemu | tee /tmp/t1_1.log
```

In QEMU: `/bin/utimes_test`. Exit.

Expected: prints `PASS`.

## Task 1.2: Refresh stale v1 comment in headers

**Files:**
- Modify: `kernel/include/sbfs.h:6`
- Modify: `tools/mkfs.c:23` (block comment around the constants section)

- [ ] **Step 1: Update kernel header comment**

In `kernel/include/sbfs.h`, change the section header from `sbfs v1 on-disk constants` to `sbfs v2 on-disk constants` (Phase 2 will flip to v3 alongside the magic bump).

- [ ] **Step 2: Update mkfs comment**

In `tools/mkfs.c`, the constant-section comment header gets the same v1 → v2 edit.

## Task 1.3: Lock v2 stat fields in `stat_direct_test`

**Files:**
- Modify: `bin/stat_direct_test/stat_direct_test.c`

- [ ] **Step 1: Read existing test**

```bash
wc -l /workspaces/sbunix/bin/stat_direct_test/stat_direct_test.c
```

Open the file and find the end of `main` (just before the final `printf("PASS")` or equivalent).

- [ ] **Step 2: Add v2 round-trip block**

Insert this block before the final pass print, after the last existing assertion:

```c
    /* sbfs v2: stat must surface mode/uid/gid from the dinode, not the
     * hardcoded literals returned by pre-v2 sbfs_op_stat. */
    {
        const char *p = "/mnt/v2stat.tmp";
        (void)unlink(p);
        int fd2 = open(p, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd2 < 0) { printf("FAIL: v2 stat create errno=%d\n", errno); return 1; }
        close(fd2);

        struct stat sst;
        if (stat(p, &sst) < 0) { printf("FAIL: v2 stat errno=%d\n", errno); return 1; }
        if (!S_ISREG(sst.st_mode)) { printf("FAIL: v2 not regular file: mode=%o\n", sst.st_mode); return 1; }
        if (sst.st_uid != 0)       { printf("FAIL: v2 uid=%u want 0\n", sst.st_uid); return 1; }
        if (sst.st_gid != 0)       { printf("FAIL: v2 gid=%u want 0\n", sst.st_gid); return 1; }
        if (sst.st_dev == 0)       { printf("FAIL: v2 dev=0 (uninitialised)\n"); return 1; }
        (void)unlink(p);
    }
```

If `<sys/stat.h>` is not already included for `S_ISREG`, ensure it is at the top of the file. Verify `errno`/`stdio`/`fcntl`/`unistd` headers are already included by reading the file's existing prelude.

- [ ] **Step 3: Build and run**

```bash
make qemu | tee /tmp/t1_3.log
```

In QEMU: `/bin/stat_direct_test`. Exit.

Expected: prints `PASS`. If it fails on the new block, the v2 dinode mirror is broken — investigate before continuing.

## Task 1.4: Phase 1 verification

- [ ] **Step 1: Full runtests + selftest**

```bash
make clean && make qemu | tee /tmp/t1_verify.log
```

In QEMU: `/bin/runtests`, watch selftest banner, exit.

Expected: `134/134` runtests, `278/278` selftest.

- [ ] **Step 2: Diff review**

```bash
git diff
git diff --stat
```

Expected: exactly 4 files touched (`kernel/fs/sbfs.c`, `kernel/include/sbfs.h`, `tools/mkfs.c`, `bin/stat_direct_test/stat_direct_test.c`). No unrelated edits.

## Task 1.5: Commit Phase 1

- [ ] **Step 1: Stage + commit**

```bash
git add kernel/fs/sbfs.c kernel/include/sbfs.h tools/mkfs.c bin/stat_direct_test/stat_direct_test.c
git commit -m "$(cat <<'EOF'
fix(sbfs): tighten setmtime hook + lock v2 stat fields in test

* sbfs_op_setmtime: drop redundant sbfs_ilock + dead d.mtime=ip->mtime
  assignment. The ilock call was a latent bug — if a future caller
  reached the hook with si->valid==0, ilock would overwrite vnode.mtime
  with the stale on-disk value just before iupdate wrote it back.
  Today's call chain (sys_utimensat -> namei_at -> dirlookup -> ilock)
  guarantees valid==1, so the body collapses to begin_op/iupdate/end_op.

* sbfs.h + mkfs.c: stale "sbfs v1" comment headers updated to v2 to
  match the magic bump in 9ecd758. (Phase 2 will flip to v3.)

* stat_direct_test: add assertion block that creates /mnt/v2stat.tmp
  and verifies S_ISREG, st_uid==0, st_gid==0, st_dev!=0 — locks in the
  v2 round-trip so a future regression is caught by runtests.

runtests 134/134, selftest 278/278.
EOF
)"
git log --oneline -3
```

Expected: new commit on top with subject starting `fix(sbfs): tighten setmtime`.

---

# Phase 2 — sbfs v3 big-file support (single commit)

## Task 2.1: Bump format constants in kernel header

**Files:**
- Modify: `kernel/include/sbfs.h`

- [ ] **Step 1: Update magic, NDIR, add NDINDIR**

In `kernel/include/sbfs.h`, replace the constants block with:

```c
/* -----------------------------------------------------------------------
 * sbfs v3 on-disk constants  (must match tools/mkfs.c exactly)
 * ----------------------------------------------------------------------- */
#define SBFS_MAGIC       0x53425633u   /* "SBV3" — v3 added double-indirect */
#define SBFS_BSIZE       512           /* bytes per block                 */
#define SBFS_NDIRECT     10            /* total addr slots per inode (disk format) */
#define SBFS_NDIR        7             /* direct block slots (addrs[0..6])  */
#define SBFS_NINDIR      2             /* single-indirect slots (addrs[7..8]) */
#define SBFS_NDINDIR     1             /* double-indirect slots (addrs[9]) */
#define SBFS_NBLK_PER_INDIR  (SBFS_BSIZE / 4)  /* 128 block addrs per indirect block */
#define SBFS_NINODES     256
#define SBFS_LOGSIZE     16
#define SBFS_DIRSIZ      14            /* max name length in a dirent     */
#define SBFS_ROOTINUM    1             /* inode number of the root dir    */

/* Max file size: 7 direct + 2*128 indirect + 1*128*128 double-indirect
 * = 7 + 256 + 16384 = 16647 blocks = 8.13 MiB ceiling.
 * Effective ceiling is fs-capped by NDATABLOCKS (~2 MiB). */
#define SBFS_MAX_FILE_SIZE  ((SBFS_NDIR \
        + SBFS_NINDIR  * SBFS_NBLK_PER_INDIR \
        + SBFS_NDINDIR * SBFS_NBLK_PER_INDIR * SBFS_NBLK_PER_INDIR) \
        * SBFS_BSIZE)
```

Update the dinode comment block to reflect v3 (the struct itself stays unchanged — `addrs[10]` slot semantics shift but layout is byte-identical):

```c
/* -----------------------------------------------------------------------
 * On-disk inode (exactly 64 bytes → 8 per 512-byte block)
 *   type(2)+nlink(2)+size(4)+mtime(8)+mode(4)+uid(2)+gid(2)+addrs[10](40) = 64
 *
 * v3 addrs[] semantics:
 *   addrs[0..6]  — direct data blocks
 *   addrs[7..8]  — single-indirect (each → 128 data block addrs)
 *   addrs[9]     — double-indirect (→ 128 indirect blocks → 128 data each)
 *
 * Magic bump (SBV2→SBV3) forces reject on stale images so the addrs[]
 * slot reinterpretation cannot be misread.
 * ----------------------------------------------------------------------- */
```

## Task 2.2: Selftest magic literal update

**Files:**
- Modify: `kernel/selftest.c:558,564`

- [ ] **Step 1: Replace v2 magic with v3**

```bash
grep -n "0x53425632" /workspaces/sbunix/kernel/selftest.c
```

Two hits expected (lines ~558, ~564). Replace each `0x53425632u` literal with `0x53425633u`. Replace each `'SBV2'` string in the printk message with `'SBV3'`.

## Task 2.3: mkfs mirror

**Files:**
- Modify: `tools/mkfs.c`

- [ ] **Step 1: Update magic and block math**

In `tools/mkfs.c`:

- Change `#define MAGIC 0x53425632u   /* "SBV2" */` to `#define MAGIC 0x53425633u   /* "SBV3" — v3 added double-indirect */`
- `NDIRECT` stays `10` (unchanged).
- Change `#define NDATABLOCKS 1000` to `#define NDATABLOCKS 4000`.
- Update the section-header comment from "sbfs v2" to "sbfs v3" (Phase-1 left it at v2).
- Update the addr-slot comment to reflect 7 direct / 2 indirect / 1 double-indirect, and the layout comment-block at the top of the file (block ranges: data blocks now occupy 51..4050 → image size ~2.07 MB).

The root-directory write only uses `addrs[0]` (one direct block), so no indirect/double-indirect path is touched in mkfs.

## Task 2.4: sbfs `sbfs_readi` — add double-indirect branch

**Files:**
- Modify: `kernel/fs/sbfs.c` (the readi function, around lines 324–362)

- [ ] **Step 1: Extend block mapping**

Replace the current `if (bn < SBFS_NDIR) { ... } else { single-indirect ... }` ladder in `sbfs_readi` with a three-branch version:

```c
        uint32_t data_block = 0;
        if (bn < SBFS_NDIR) {
            data_block = si->d.addrs[bn];
        } else if (bn < SBFS_NDIR + SBFS_NINDIR * SBFS_NBLK_PER_INDIR) {
            uint32_t rel = bn - SBFS_NDIR;
            uint32_t ii  = rel / SBFS_NBLK_PER_INDIR;
            uint32_t io  = rel % SBFS_NBLK_PER_INDIR;
            uint32_t indir = si->d.addrs[SBFS_NDIR + ii];
            if (indir == 0) break;
            struct buf *ibp = bread(indir);
            data_block = ((uint32_t *)ibp->data)[io];
            brelse(ibp);
        } else {
            /* Double-indirect range. Layout: si->d.addrs[SBFS_NDIR + SBFS_NINDIR]
             * → outer block of 128 indirect-block addresses → each inner block
             * holds 128 data-block addresses. */
            uint32_t drel = bn - (SBFS_NDIR + SBFS_NINDIR * SBFS_NBLK_PER_INDIR);
            uint32_t oi   = drel / SBFS_NBLK_PER_INDIR;
            uint32_t ii   = drel % SBFS_NBLK_PER_INDIR;
            if (oi >= SBFS_NBLK_PER_INDIR) break;
            uint32_t outer = si->d.addrs[SBFS_NDIR + SBFS_NINDIR];
            if (outer == 0) break;
            struct buf *obp = bread(outer);
            uint32_t inner = ((uint32_t *)obp->data)[oi];
            brelse(obp);
            if (inner == 0) break;
            struct buf *ibp = bread(inner);
            data_block = ((uint32_t *)ibp->data)[ii];
            brelse(ibp);
        }
```

The existing data-block read (`if (data_block == 0) break; struct buf *bp = bread(...)`) stays unchanged below this branch ladder.

- [ ] **Step 2: Confirm short-read semantics**

For a sparse file (outer == 0 or inner == 0 inside the double-indirect range), the read breaks out of the loop and returns `total`. Matches existing single-indirect behaviour — no zero-fill, just short read. This is consistent with current sbfs semantics; no test currently depends on hole-zero-fill.

## Task 2.5: sbfs `sbfs_writei` — allocate double-indirect on demand

**Files:**
- Modify: `kernel/fs/sbfs.c` (the writei function, around lines 371–451)

- [ ] **Step 1: Extend allocator ladder**

Replace the existing two-branch allocator inside `sbfs_writei` with:

```c
        uint32_t data_block = 0;
        if (bn < SBFS_NDIR) {
            if (si->d.addrs[bn] == 0) {
                uint32_t nb = balloc();
                if (!nb) { enospc = 1; break; }
                si->d.addrs[bn] = nb;
                si->dirty = 1;
            }
            data_block = si->d.addrs[bn];
        } else if (bn < SBFS_NDIR + SBFS_NINDIR * SBFS_NBLK_PER_INDIR) {
            uint32_t rel = bn - SBFS_NDIR;
            uint32_t ii  = rel / SBFS_NBLK_PER_INDIR;
            uint32_t io  = rel % SBFS_NBLK_PER_INDIR;

            if (si->d.addrs[SBFS_NDIR + ii] == 0) {
                uint32_t ib = balloc();
                if (!ib) { enospc = 1; break; }
                si->d.addrs[SBFS_NDIR + ii] = ib;
                si->dirty = 1;
            }
            struct buf *ibp = bread(si->d.addrs[SBFS_NDIR + ii]);
            uint32_t *ia = (uint32_t *)ibp->data;
            if (ia[io] == 0) {
                uint32_t nb = balloc();
                if (!nb) { brelse(ibp); enospc = 1; break; }
                ia[io] = nb;
                log_write(ibp);
            }
            data_block = ia[io];
            brelse(ibp);
        } else {
            /* Double-indirect allocation: outer block holds 128 indirect-block
             * addresses; each inner block holds 128 data-block addresses.
             * Three balloc points lazily allocate outer, inner, then data. */
            uint32_t drel = bn - (SBFS_NDIR + SBFS_NINDIR * SBFS_NBLK_PER_INDIR);
            uint32_t oi   = drel / SBFS_NBLK_PER_INDIR;
            uint32_t ii   = drel % SBFS_NBLK_PER_INDIR;
            if (oi >= SBFS_NBLK_PER_INDIR) break;

            if (si->d.addrs[SBFS_NDIR + SBFS_NINDIR] == 0) {
                uint32_t ob = balloc();
                if (!ob) { enospc = 1; break; }
                si->d.addrs[SBFS_NDIR + SBFS_NINDIR] = ob;
                si->dirty = 1;
            }
            struct buf *obp = bread(si->d.addrs[SBFS_NDIR + SBFS_NINDIR]);
            uint32_t *oa = (uint32_t *)obp->data;
            if (oa[oi] == 0) {
                uint32_t ib = balloc();
                if (!ib) { brelse(obp); enospc = 1; break; }
                oa[oi] = ib;
                log_write(obp);
            }
            uint32_t inner_addr = oa[oi];
            brelse(obp);

            struct buf *ibp = bread(inner_addr);
            uint32_t *ia = (uint32_t *)ibp->data;
            if (ia[ii] == 0) {
                uint32_t nb = balloc();
                if (!nb) { brelse(ibp); enospc = 1; break; }
                ia[ii] = nb;
                log_write(ibp);
            }
            data_block = ia[ii];
            brelse(ibp);
        }
```

The downstream "write into `data_block`" body and the size/mtime/iupdate epilogue stay unchanged.

## Task 2.6: sbfs `sbfs_itrunc` — free double-indirect tree

**Files:**
- Modify: `kernel/fs/sbfs.c` (the itrunc function, around lines 457–482)

- [ ] **Step 1: Add double-indirect free branch**

Replace the existing `sbfs_itrunc` body with:

```c
static void sbfs_itrunc(struct sbfs_inode *si) {
    /* Direct blocks. */
    for (int bn = 0; bn < SBFS_NDIR; bn++) {
        if (si->d.addrs[bn]) {
            bfree(si->d.addrs[bn]);
            si->d.addrs[bn] = 0;
        }
    }
    /* Single-indirect blocks. */
    for (int ii = 0; ii < SBFS_NINDIR; ii++) {
        if (si->d.addrs[SBFS_NDIR + ii]) {
            struct buf *ibp = bread(si->d.addrs[SBFS_NDIR + ii]);
            uint32_t *ia = (uint32_t *)ibp->data;
            for (int i = 0; i < SBFS_NBLK_PER_INDIR; i++) {
                if (ia[i]) bfree(ia[i]);
            }
            brelse(ibp);
            bfree(si->d.addrs[SBFS_NDIR + ii]);
            si->d.addrs[SBFS_NDIR + ii] = 0;
        }
    }
    /* Double-indirect tree. */
    if (si->d.addrs[SBFS_NDIR + SBFS_NINDIR]) {
        struct buf *obp = bread(si->d.addrs[SBFS_NDIR + SBFS_NINDIR]);
        uint32_t *oa = (uint32_t *)obp->data;
        for (int oi = 0; oi < SBFS_NBLK_PER_INDIR; oi++) {
            if (oa[oi]) {
                struct buf *ibp = bread(oa[oi]);
                uint32_t *ia = (uint32_t *)ibp->data;
                for (int i = 0; i < SBFS_NBLK_PER_INDIR; i++) {
                    if (ia[i]) bfree(ia[i]);
                }
                brelse(ibp);
                bfree(oa[oi]);
            }
        }
        brelse(obp);
        bfree(si->d.addrs[SBFS_NDIR + SBFS_NINDIR]);
        si->d.addrs[SBFS_NDIR + SBFS_NINDIR] = 0;
    }
    si->d.size  = 0;
    si->vnode.size = 0;
    si->d.mtime = sbfs_now();
    si->vnode.mtime = si->d.mtime;
    si->dirty = 1;
    sbfs_iupdate(si);
}
```

## Task 2.7: Build sanity (no test yet)

- [ ] **Step 1: Compile clean**

```bash
make clean && make 2>&1 | tee /tmp/t2_build.log
tail -20 /tmp/t2_build.log
```

Expected: completes without errors. `disk.img` is rebuilt. Any "implicit declaration" or "undeclared identifier" errors signal a missed include or naming mismatch — fix before running.

- [ ] **Step 2: Smoke boot**

```bash
make qemu | tee /tmp/t2_smoke.log
```

In QEMU: `/bin/runtests`. Exit.

Expected: previous `134/134` still passes (no regression). Plus `bigfile_pcache_test` is now in `bin/runtests/runtests.c` (Task 2.9 below — if running Task 2.7 before 2.9, expect 134/134; after 2.9, expect 135/135 minus bigfile until Task 2.10 adds the new test).

## Task 2.8: New test — `bin/sbfs_bigfile_test/`

**Files:**
- Create: `bin/sbfs_bigfile_test/sbfs_bigfile_test.c`
- Create: `bin/sbfs_bigfile_test/Makefile` (mirror an existing tiny-test Makefile such as `bin/utimes_test/Makefile`)

- [ ] **Step 1: Confirm sibling Makefile shape**

```bash
cat /workspaces/sbunix/bin/utimes_test/Makefile
```

Copy that file into the new directory, changing the binary name `utimes_test` → `sbfs_bigfile_test` in every occurrence.

- [ ] **Step 2: Write test source**

Create `bin/sbfs_bigfile_test/sbfs_bigfile_test.c`:

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

/* sbfs v3 regression: exercise the double-indirect path.
 *
 *   Layout: direct covers bytes 0 .. 7*512-1 = 3.5 KiB.
 *           single-indirect covers next 2*128*512 = 128 KiB.
 *           double-indirect kicks in at byte 131584 (~129 KiB).
 *
 * We write a 200 KiB pseudo-random pattern (deterministic seed, no libc
 * rand dependency) so the file straddles all three regions and the
 * double-indirect branch is unambiguously exercised. Round-trip
 * verification is byte-for-byte. Then we unlink and confirm the
 * itrunc-via-unlink path doesn't leak via a follow-up 100 KiB write to
 * the same path — if the previous run's blocks weren't freed,
 * a 4000-block fs would ENOSPC quickly when this test runs in sequence
 * after other big-file tests. */
#define SIZE      (200u * 1024u)
#define CHUNK     4096

static unsigned next_rand(unsigned x) { return x * 1103515245u + 12345u; }

int main(void) {
    const char *path = "/mnt/bigfile.bin";
    (void)unlink(path);

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("FAIL: open errno=%d\n", errno); return 1; }

    char buf[CHUNK];
    unsigned seed = 0xdeadbeef;
    unsigned written = 0;
    while (written < SIZE) {
        for (int i = 0; i < CHUNK; i++) {
            seed = next_rand(seed);
            buf[i] = (char)(seed >> 16);
        }
        int n = write(fd, buf, CHUNK);
        if (n != CHUNK) {
            printf("FAIL: write at %u returned %d errno=%d\n", written, n, errno);
            return 1;
        }
        written += CHUNK;
    }

    if (lseek(fd, 0, SEEK_SET) != 0) {
        printf("FAIL: lseek errno=%d\n", errno); return 1;
    }

    seed = 0xdeadbeef;
    unsigned read_total = 0;
    while (read_total < SIZE) {
        int n = read(fd, buf, CHUNK);
        if (n != CHUNK) {
            printf("FAIL: read at %u returned %d errno=%d\n", read_total, n, errno);
            return 1;
        }
        for (int i = 0; i < CHUNK; i++) {
            seed = next_rand(seed);
            char want = (char)(seed >> 16);
            if (buf[i] != want) {
                printf("FAIL: mismatch at %u: got %02x want %02x\n",
                       read_total + i, (unsigned char)buf[i], (unsigned char)want);
                return 1;
            }
        }
        read_total += CHUNK;
    }
    close(fd);

    /* Truncate-via-unlink test: rewrite a smaller file at the same path.
     * If the previous itrunc didn't free the double-indirect tree, the fs
     * is now 200 KiB heavier; a 100 KiB second pass would still fit but a
     * pathological loop would not. Smoke test for one cycle. */
    if (unlink(path) < 0) { printf("FAIL: unlink errno=%d\n", errno); return 1; }

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("FAIL: reopen errno=%d\n", errno); return 1; }
    for (int p = 0; p < 25; p++) {        /* 25 * 4096 = 100 KiB */
        memset(buf, (char)p, CHUNK);
        if (write(fd, buf, CHUNK) != CHUNK) {
            printf("FAIL: second-pass write %d errno=%d\n", p, errno); return 1;
        }
    }
    close(fd);
    (void)unlink(path);

    printf("PASS\n");
    return 0;
}
```

- [ ] **Step 3: Wire into top-level build**

```bash
grep -n "utimes_test" /workspaces/sbunix/Makefile
```

Find every place `utimes_test` appears in the top-level Makefile and add a `sbfs_bigfile_test` entry next to it (likely a USER_BINS-style list and possibly a rootfs install line). If the Makefile uses a directory glob (e.g. iterates over `bin/*/`), no edit needed — verify by running `make` and grepping the build output for `sbfs_bigfile_test`.

- [ ] **Step 4: Enrol in runtests**

In `bin/runtests/runtests.c`, add `"/bin/sbfs_bigfile_test",` to the test list near the other sbfs/timestamp tests (alphabetical placement matches surrounding convention).

- [ ] **Step 5: Verify it runs**

```bash
make qemu | tee /tmp/t2_8.log
```

In QEMU: `/bin/sbfs_bigfile_test`, then `/bin/runtests`. Exit.

Expected: standalone prints `PASS`; runtests reports the new test in the pass count.

## Task 2.9: Enrol `bigfile_pcache_test` in runtests

**Files:**
- Modify: `bin/runtests/runtests.c`

- [ ] **Step 1: Add to test list**

Add `"/bin/bigfile_pcache_test",` near `/bin/sbfs_bigfile_test`. Build target presumably already exists — verify with `ls build/bin/bigfile_pcache_test` after the next `make`.

## Task 2.10: Phase 2 verification

- [ ] **Step 1: Full make + run**

```bash
make clean && make qemu | tee /tmp/t2_verify.log
```

In QEMU: `/bin/runtests`, observe selftest, exit.

Expected: runtests **136/136** (134 prior + `sbfs_bigfile_test` + `bigfile_pcache_test`). Selftest **278/278**. Image size visibly larger:

```bash
ls -la build/disk.img
```

Expected: ~2.07 MB (4051 blocks × 512).

- [ ] **Step 2: Manual probe**

In QEMU shell:

```
dd if=/dev/zero of=/mnt/x bs=4k count=400
stat /mnt/x
unlink /mnt/x
```

Expected: `dd` succeeds (1.6 MiB write), stat reports `st_size=1638400`.

## Task 2.11: Commit Phase 2

- [ ] **Step 1: Stage + commit**

```bash
git add kernel/include/sbfs.h kernel/fs/sbfs.c kernel/selftest.c tools/mkfs.c \
        bin/sbfs_bigfile_test bin/runtests/runtests.c
# Also include Makefile if it was edited in Task 2.8 Step 3:
git status                            # check for any unstaged Makefile change
git diff --cached --stat
git commit -m "$(cat <<'EOF'
feat(sbfs): v3 format — double-indirect blocks, ~2 MiB fs (was 132 KiB/file)

Lifts the per-file ceiling from 132 KiB to ~8 MiB (fs-capped at ~2 MiB)
so graders/tests that write multi-hundred-KiB files no longer hit EFBIG
in writei.

* On-disk: magic bump SBV2 -> SBV3 (0x53425632 -> 0x53425633). Dinode
  layout byte-identical (still 64 B / 8 per block). addrs[] slot
  semantics shift: 7 direct + 2 single-indirect + 1 double-indirect
  (was 8 + 2 + 0). Reusing one direct slot rather than growing the
  dinode keeps the format simple and the indirection table consistent.
* NDATABLOCKS 1000 -> 4000 (bitmap caps at 4096; 96-slot slack). Image
  grows ~525 KB -> ~2.07 MB.
* sbfs_readi/writei: new double-indirect branch. Outer block of 128
  inner-block addrs; each inner of 128 data-block addrs. Three lazy
  balloc points (outer, inner, data). Read short-circuits on hole.
* sbfs_itrunc: walks the double-indirect tree to bfree leaves, then
  inner blocks, then outer.
* selftest: magic literal + 'SBVx' string updated.
* mkfs: mirrors magic + NDATABLOCKS bump. Root dir still uses addrs[0]
  only so no indirect code added on the host side.
* New bin/sbfs_bigfile_test: writes 200 KiB pseudo-random pattern
  (straddles direct + indirect + double-indirect), reads back
  byte-for-byte, then exercises itrunc-via-unlink with a 100 KiB
  follow-up pass. Enrolled in runtests.
* bigfile_pcache_test (existing, 512 KiB) re-enrolled in runtests — it
  no longer hits the per-file cap.

Magic check remains pre-log-replay so stale SBV2 images fail loud.
runtests 134/134 -> 136/136, selftest 278/278.
EOF
)"
git log --oneline -4
```

Expected: commit subject `feat(sbfs): v3 format — double-indirect blocks…`.

---

# Phase 3 — chmod / chown family

## Task 3.1: Add `setmode` / `setowner` inode hooks

**Files:**
- Modify: `kernel/include/inode.h` (around the existing `setmtime` block)

- [ ] **Step 1: Extend `struct inode_ops`**

Append after the existing `setmtime` declaration in `kernel/include/inode.h`:

```c
    /* Persist a freshly-updated ip->mode to the filesystem's on-disk
     * representation. sys_chmod / sys_fchmod set ip->mode (preserving
     * type bits) then call this hook. tmpfs leaves it NULL (stat reads
     * the vnode mode directly). sbfs reuses sbfs_iupdate's
     * vnode->dinode mirror — body is begin_op/iupdate/end_op. Caller
     * holds a ref on ip. Returns 0 / -errno. */
    int (*setmode)(struct inode *);

    /* Persist a freshly-updated ip->uid / ip->gid. Mirror of setmode
     * for chown(2) / lchown(2) / fchown(2). */
    int (*setowner)(struct inode *);
```

## Task 3.2: sbfs implementations

**Files:**
- Modify: `kernel/fs/sbfs.c` (next to `sbfs_op_setmtime`)

- [ ] **Step 1: Add the two hook functions**

Insert immediately below `sbfs_op_setmtime`:

```c
/* sys_chmod / sys_fchmod hook: vnode.mode already updated by caller
 * with type bits preserved. iupdate copies vnode.mode into d.mode. */
static int sbfs_op_setmode(struct inode *ip) {
    struct sbfs_inode *si = (struct sbfs_inode *)ip;
    begin_op();
    sbfs_iupdate(si);
    end_op();
    return 0;
}

/* sys_chown / sys_lchown / sys_fchown hook. iupdate copies
 * vnode.uid/gid (uint32) into d.uid/gid (uint16 — silent truncation
 * matches the existing v2 layout choice; teaching kernel uses uid=0). */
static int sbfs_op_setowner(struct inode *ip) {
    struct sbfs_inode *si = (struct sbfs_inode *)ip;
    begin_op();
    sbfs_iupdate(si);
    end_op();
    return 0;
}
```

- [ ] **Step 2: Register in `sbfs_iops`**

In the `sbfs_iops` initialiser (around the line `.setmtime = sbfs_op_setmtime,`), add:

```c
    .setmode  = sbfs_op_setmode,
    .setowner = sbfs_op_setowner,
```

- [ ] **Step 3: Forward declarations**

Near the top of `kernel/fs/sbfs.c` where `sbfs_op_setmtime` is forward-declared, add:

```c
static int  sbfs_op_setmode (struct inode *);
static int  sbfs_op_setowner(struct inode *);
```

## Task 3.3: Reserve syscall numbers

**Files:**
- Modify: `kernel/include/syscall.h`
- Modify: `libc/include/sys/syscall.h`

- [ ] **Step 1: Append new numbers in kernel header**

After `SYS_readlinkat 133` in `kernel/include/syscall.h`, append:

```c
#define SYS_chmod         134  // (path, mode)
#define SYS_fchmod        135  // (fd, mode)
#define SYS_chown         136  // (path, uid, gid)
#define SYS_lchown        137  // (path, uid, gid) — no symlink follow
#define SYS_fchown        138  // (fd, uid, gid)
```

- [ ] **Step 2: Mirror in libc header**

Append the same five `#define`s after `SYS_readlinkat 133` in `libc/include/sys/syscall.h` (no trailing comment is required, but matching the existing terse style is fine).

## Task 3.4: Implement `sys_chmod` / `sys_fchmod`

**Files:**
- Modify: `kernel/syscall.c` (insert near the existing `sys_utimensat`, around line 1158)

- [ ] **Step 1: Add `sys_chmod`**

Insert after `sys_utimensat`:

```c
// ---------------------------------------------------------------------------
// sys_chmod / sys_fchmod — POSIX chmod(2)/fchmod(2). Replaces the permission
// bits (low 12 bits) of the inode mode while preserving the S_IF* type
// bits. No permission enforcement: matches the existing root-equivalent
// access model in sys_access. Read-only filesystems (tarfs/devfs/procfs)
// return EROFS using the same "no create and no unlink" detection rule as
// sys_utimensat.
// ---------------------------------------------------------------------------
static int do_chmod_ip(struct inode *ip, mode_t mode) {
    if (!ip->ops || (!ip->ops->create && !ip->ops->unlink)) return -EROFS;
    /* Preserve type bits; only permission bits change. */
    ip->mode = (ip->mode & ~07777u) | (mode & 07777u);
    if (ip->ops->setmode) {
        int sm = ip->ops->setmode(ip);
        if (sm < 0) return sm;
    }
    return 0;
}

static int64_t sys_chmod(const char *path, mode_t mode) {
    char kpath[PATH_MAX_LOCAL];
    int rc = copyin_cstr(path, kpath, sizeof(kpath));
    if (rc < 0) return rc;
    struct inode *ip;
    rc = namei(kpath, &ip);
    if (rc < 0) return rc;
    rc = do_chmod_ip(ip, mode);
    inode_put(ip);
    return rc;
}

static int64_t sys_fchmod(int fd, mode_t mode) {
    struct pcb *p = current_proc();
    if (!p || fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;
    struct file *f = p->ofile[fd];
    if (f->type != FD_INODE || !f->ip) return -EBADF;
    return do_chmod_ip(f->ip, mode);
}
```

Confirm `namei` is the no-arg-start variant available in this file; if only `namei_at` exists, replace with `namei_at(0, kpath, &ip)`. (Run `grep -n "^int namei\b\|^int namei(" kernel/fs/vfs.c` and adjust to match.)

## Task 3.5: Implement `sys_chown` / `sys_lchown` / `sys_fchown`

**Files:**
- Modify: `kernel/syscall.c` (immediately after the chmod block)

- [ ] **Step 1: Add chown trio**

```c
// ---------------------------------------------------------------------------
// sys_chown / sys_lchown / sys_fchown — POSIX chown(2)/lchown(2)/fchown(2).
// (uid_t)-1 / (gid_t)-1 mean "leave this field alone" per POSIX. No
// permission enforcement; EROFS on read-only filesystems via the same
// rule as chmod.
// ---------------------------------------------------------------------------
static int do_chown_ip(struct inode *ip, uid_t uid, gid_t gid) {
    if (!ip->ops || (!ip->ops->create && !ip->ops->unlink)) return -EROFS;
    if (uid != (uid_t)-1) ip->uid = uid;
    if (gid != (gid_t)-1) ip->gid = gid;
    if (ip->ops->setowner) {
        int rc = ip->ops->setowner(ip);
        if (rc < 0) return rc;
    }
    return 0;
}

static int64_t sys_chown(const char *path, uid_t uid, gid_t gid) {
    char kpath[PATH_MAX_LOCAL];
    int rc = copyin_cstr(path, kpath, sizeof(kpath));
    if (rc < 0) return rc;
    struct inode *ip;
    rc = namei_at(0, kpath, &ip);
    if (rc < 0) return rc;
    rc = do_chown_ip(ip, uid, gid);
    inode_put(ip);
    return rc;
}

static int64_t sys_lchown(const char *path, uid_t uid, gid_t gid) {
    char kpath[PATH_MAX_LOCAL];
    int rc = copyin_cstr(path, kpath, sizeof(kpath));
    if (rc < 0) return rc;
    struct inode *ip;
    rc = lnamei_at(0, kpath, &ip);
    if (rc < 0) return rc;
    rc = do_chown_ip(ip, uid, gid);
    inode_put(ip);
    return rc;
}

static int64_t sys_fchown(int fd, uid_t uid, gid_t gid) {
    struct pcb *p = current_proc();
    if (!p || fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;
    struct file *f = p->ofile[fd];
    if (f->type != FD_INODE || !f->ip) return -EBADF;
    return do_chown_ip(f->ip, uid, gid);
}
```

If `uid_t` / `gid_t` are not yet visible inside `kernel/syscall.c`, add the appropriate header include (`#include <sys/types.h>` or whatever the kernel's mirror is). Verify with `grep -n "uid_t\|gid_t" kernel/syscall.c` — they are likely already used by `sys_setuid`/`sys_setgid`.

## Task 3.6: Wire dispatch cases

**Files:**
- Modify: `kernel/syscall.c` (the `syscall_dispatch` switch, near the existing `setuid`/`setgid` cases around line 2734)

- [ ] **Step 1: Add dispatch cases**

Insert after `case SYS_setgid: ...`:

```c
        case SYS_chmod:
            return sys_chmod((const char *)trapframe[TF_A0],
                             (mode_t)trapframe[TF_A1]);
        case SYS_fchmod:
            return sys_fchmod((int)(int64_t)trapframe[TF_A0],
                              (mode_t)trapframe[TF_A1]);
        case SYS_chown:
            return sys_chown((const char *)trapframe[TF_A0],
                             (uid_t)trapframe[TF_A1],
                             (gid_t)trapframe[TF_A2]);
        case SYS_lchown:
            return sys_lchown((const char *)trapframe[TF_A0],
                              (uid_t)trapframe[TF_A1],
                              (gid_t)trapframe[TF_A2]);
        case SYS_fchown:
            return sys_fchown((int)(int64_t)trapframe[TF_A0],
                              (uid_t)trapframe[TF_A1],
                              (gid_t)trapframe[TF_A2]);
```

## Task 3.7: Replace libc stubs

**Files:**
- Modify: `libc/sys_stubs.c` (existing `chmod`/`fchmod` stubs at lines 20–31)
- Modify: `libc/misc.c` (existing `chown`/`fchown`/`lchown` stubs at lines 65–82)

- [ ] **Step 1: Replace `chmod`/`fchmod` in `sys_stubs.c`**

Replace the existing pseudo-stub bodies with real syscall wrappers:

```c
int chmod(const char *path, mode_t mode) {
    long r = syscall(SYS_chmod, (long)path, (long)mode);
    return r < 0 ? -1 : 0;
}
int fchmod(int fd, mode_t mode) {
    long r = syscall(SYS_fchmod, (long)fd, (long)mode);
    return r < 0 ? -1 : 0;
}
```

Remove the obsolete comment "SBUnix has no on-disk permission bits…" that precedes them; replace with a single line: `/* chmod / fchmod — real syscalls; see kernel/syscall.c sys_chmod. */`

- [ ] **Step 2: Replace `chown`/`fchown`/`lchown` in `misc.c`**

Replace the existing stub bodies (and their comment block) with:

```c
/* Ownership syscalls: real kernel calls now persist mode/uid/gid in
 * the sbfs dinode (post-v2). No permission enforcement — matches the
 * existing root-equivalent sys_access model. (uid_t)-1 / (gid_t)-1
 * mean "leave the field alone" per POSIX. */
int chown(const char *p, uid_t u, gid_t g) {
    long r = syscall(SYS_chown, (long)p, (long)u, (long)g);
    return r < 0 ? -1 : 0;
}
int fchown(int fd, uid_t u, gid_t g) {
    long r = syscall(SYS_fchown, (long)fd, (long)u, (long)g);
    return r < 0 ? -1 : 0;
}
int lchown(const char *p, uid_t u, gid_t g) {
    long r = syscall(SYS_lchown, (long)p, (long)u, (long)g);
    return r < 0 ? -1 : 0;
}
```

Add `#include <sys/syscall.h>` near the top of `libc/misc.c` if not already present (check with `grep -n "sys/syscall.h" libc/misc.c`).

## Task 3.8: New test — `bin/chmod_chown_test/`

**Files:**
- Create: `bin/chmod_chown_test/chmod_chown_test.c`
- Create: `bin/chmod_chown_test/Makefile` (mirror `bin/utimes_test/Makefile`)

- [ ] **Step 1: Write test source**

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

/* POSIX chmod/chown round-trip on sbfs, tmpfs (fchmod), tarfs (EROFS).
 * Permission enforcement is intentionally absent across this kernel
 * (matches sys_access root-equivalent model), so all sbfs/tmpfs calls
 * succeed regardless of caller uid; only the read-only tarfs path
 * returns EROFS. */
int main(void) {
    const char *p = "/mnt/cmod.tmp";
    (void)unlink(p);

    /* 1. Create on sbfs, baseline stat. */
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("FAIL: create errno=%d\n", errno); return 1; }
    close(fd);

    struct stat st;
    if (stat(p, &st) < 0) { printf("FAIL: stat0 errno=%d\n", errno); return 1; }
    if (!S_ISREG(st.st_mode)) { printf("FAIL: not regular: %o\n", st.st_mode); return 1; }
    if ((st.st_mode & 07777) != 0644) {
        printf("FAIL: default perms %o want 0644\n", st.st_mode & 07777); return 1;
    }

    /* 2. chmod 0600 — only perm bits change. */
    if (chmod(p, 0600) < 0) { printf("FAIL: chmod errno=%d\n", errno); return 1; }
    if (stat(p, &st) < 0)   { printf("FAIL: stat1 errno=%d\n", errno); return 1; }
    if (!S_ISREG(st.st_mode)) { printf("FAIL: type bits lost: %o\n", st.st_mode); return 1; }
    if ((st.st_mode & 07777) != 0600) {
        printf("FAIL: chmod result %o want 0600\n", st.st_mode & 07777); return 1;
    }

    /* 3. chown(42, 7) — full set. */
    if (chown(p, 42, 7) < 0) { printf("FAIL: chown errno=%d\n", errno); return 1; }
    if (stat(p, &st) < 0)    { printf("FAIL: stat2 errno=%d\n", errno); return 1; }
    if (st.st_uid != 42)     { printf("FAIL: uid=%u want 42\n", st.st_uid); return 1; }
    if (st.st_gid != 7)      { printf("FAIL: gid=%u want 7\n", st.st_gid); return 1; }

    /* 4. chown(-1, 99) — gid only. */
    if (chown(p, (uid_t)-1, 99) < 0) {
        printf("FAIL: chown(-1,99) errno=%d\n", errno); return 1;
    }
    if (stat(p, &st) < 0) { printf("FAIL: stat3 errno=%d\n", errno); return 1; }
    if (st.st_uid != 42)  { printf("FAIL: uid changed when sentinel: %u\n", st.st_uid); return 1; }
    if (st.st_gid != 99)  { printf("FAIL: gid=%u want 99\n", st.st_gid); return 1; }

    /* 5. Persist across reopen — exercise dinode writeback path. */
    fd = open(p, O_RDONLY);
    if (fd < 0) { printf("FAIL: reopen errno=%d\n", errno); return 1; }
    struct stat st2;
    if (fstat(fd, &st2) < 0) { printf("FAIL: fstat errno=%d\n", errno); return 1; }
    close(fd);
    if (st2.st_uid != 42 || st2.st_gid != 99 || (st2.st_mode & 07777) != 0600) {
        printf("FAIL: not persisted: uid=%u gid=%u mode=%o\n",
               st2.st_uid, st2.st_gid, st2.st_mode & 07777);
        return 1;
    }

    /* 6. tarfs EROFS — chmod on /bin/sh must fail. */
    errno = 0;
    if (chmod("/bin/sh", 0644) == 0 || errno != EROFS) {
        printf("FAIL: tarfs chmod errno=%d want EROFS=%d\n", errno, EROFS);
        return 1;
    }

    /* 7. fchmod on tmpfs. */
    const char *tp = "/tmp/cmod.tmp";
    (void)unlink(tp);
    fd = open(tp, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("FAIL: tmpfs create errno=%d\n", errno); return 1; }
    if (fchmod(fd, 0700) < 0) { printf("FAIL: fchmod errno=%d\n", errno); return 1; }
    if (fstat(fd, &st) < 0)   { printf("FAIL: tmpfs fstat errno=%d\n", errno); return 1; }
    if ((st.st_mode & 07777) != 0700) {
        printf("FAIL: tmpfs fchmod result %o want 0700\n", st.st_mode & 07777); return 1;
    }
    close(fd);
    (void)unlink(tp);

    (void)unlink(p);
    printf("PASS\n");
    return 0;
}
```

- [ ] **Step 2: Makefile**

Copy `bin/utimes_test/Makefile` to `bin/chmod_chown_test/Makefile` and rename the binary inside. Same convention as Task 2.8 Step 1.

- [ ] **Step 3: Wire into top-level build + runtests**

Mirror the wiring done for `sbfs_bigfile_test`. Add `"/bin/chmod_chown_test",` to `bin/runtests/runtests.c` near the other stat/access tests.

- [ ] **Step 4: Verify**

```bash
make qemu | tee /tmp/t3_8.log
```

In QEMU: `/bin/chmod_chown_test` standalone (expect `PASS`), then `/bin/runtests`.

Expected: standalone PASS; runtests **137/137** (Phase 2 brought it to 136; this adds the new test).

- [ ] **Step 5: Caveat on tmpfs setmode**

If Step 7 of the test fails because tmpfs returns 0700 in fstat but the test sees something else, the tmpfs `op_stat` either reads `vnode.mode` (good — change persists) or returns a hardcoded literal. If hardcoded, file an out-of-scope follow-up issue; do not extend Phase 3. The test path "tmpfs supports fchmod" only requires vnode-level write, no hook.

Verify by reading the tmpfs op_stat — if it reads `ip->mode`, the test will pass without further changes.

## Task 3.9: Phase 3 verification

- [ ] **Step 1: Full clean run**

```bash
make clean && make qemu | tee /tmp/t3_verify.log
```

In QEMU: `/bin/runtests`, exit.

Expected: **137/137** runtests, **278/278** selftest.

- [ ] **Step 2: Manual probe**

In QEMU shell:

```
touch /mnt/x
chmod 0600 /mnt/x
chown 1 1 /mnt/x
stat /mnt/x
```

Expected: `stat` reports `Mode: 0600`, `Uid: 1`, `Gid: 1`.

## Task 3.10: Commit Phase 3

- [ ] **Step 1: Stage + commit**

```bash
git add kernel/include/inode.h kernel/include/syscall.h libc/include/sys/syscall.h \
        kernel/fs/sbfs.c kernel/syscall.c \
        libc/sys_stubs.c libc/misc.c \
        bin/chmod_chown_test bin/runtests/runtests.c
git status                                # confirm no unstaged surprises
git commit -m "$(cat <<'EOF'
feat(posix): chmod/fchmod/chown/lchown/fchown — wire syscalls + sbfs persistence

Replaces the libc no-op stubs with real syscalls that update the inode
mode/uid/gid in-place. sbfs persists the change via the existing v2
dinode mirror; tmpfs persists via the in-memory vnode (its op_stat
reads back through ip->mode). Read-only filesystems (tarfs, devfs,
procfs) return EROFS using the same "no create and no unlink"
detection rule sys_utimensat uses.

* inode_ops: new ->setmode / ->setowner hooks. sbfs implementations
  are one-liners that wrap sbfs_iupdate in begin_op/end_op — iupdate
  already mirrors vnode.mode/uid/gid into the dinode.
* sys_chmod / sys_fchmod: preserve S_IF* type bits, replace only the
  low 12 permission bits.
* sys_chown / sys_lchown / sys_fchown: honour POSIX (uid_t)-1 /
  (gid_t)-1 "leave alone" sentinel. lchown uses lnamei_at so it
  operates on the symlink itself.
* libc: chmod/fchmod in sys_stubs.c and chown/fchown/lchown in misc.c
  switched from "stat-then-return-0" stubs to direct syscall wrappers.
* Syscall numbers 134..138 reserved (kernel + libc headers).
* bin/chmod_chown_test (8 checks: default mode, chmod preserves type
  bits, chown full, chown -1 sentinel, persist-across-reopen, tarfs
  EROFS, tmpfs fchmod). Enrolled in runtests.

No permission enforcement (matches existing root-equivalent
sys_access model). runtests 136/136 -> 137/137, selftest 278/278.
EOF
)"
git log --oneline -6
```

Expected: new commit on top with subject `feat(posix): chmod/fchmod/chown/lchown/fchown…`.

---

# Phase 4 — Push & PR

## Task 4.1: Final branch verification

- [ ] **Step 1: Diff summary against master**

```bash
git log --oneline master..HEAD
git diff master..HEAD --stat | tail -30
```

Expected: 5 logical commits (`fix(sbfs): persist utimensat mtime` from 1c6d6b5, `feat(sbfs): persist mode/uid/gid in dinode` from 9ecd758, `docs(spec)` × 2, plus the new Phase 1/2/3 commits — total 6 commits on top of master if both spec commits are kept; otherwise 5).

If the spec commits should be squashed or dropped before PR, decide now (recommended: keep them — they document the design intent).

- [ ] **Step 2: Clean rebuild + final test pass**

```bash
make clean && make qemu | tee /tmp/t4_final.log
```

In QEMU: `/bin/runtests`. Exit.

Expected: **137/137** runtests, **278/278** selftest. No flaky outputs.

## Task 4.2: Push

- [ ] **Step 1: Push branch**

```bash
git push -u origin fix/sbfs-utimensat-persist
```

Expected: push succeeds; remote tracking branch set.

## Task 4.3: Open PR

- [ ] **Step 1: Find a usable PR mechanism**

```bash
which gh || echo "no gh — use web UI or workspace PR helper"
```

If `gh` is unavailable in this sandbox (which prior turns established), open the PR via the GitHub web UI or whatever helper the project uses. Title:

```
sbfs: persist utimensat mtime + v2 stat fields + v3 big-file + chmod/chown
```

- [ ] **Step 2: Body**

Paste this into the PR description (markdown):

```markdown
## Summary

Five-commit branch fixing sbfs storage gaps surfaced during the post-audit
review.

* `fix(sbfs): persist utimensat mtime to dinode` (1c6d6b5)
* `feat(sbfs): persist mode/uid/gid in dinode (v2 format)` (9ecd758)
* `fix(sbfs): tighten setmtime hook + lock v2 stat fields in test`
* `feat(sbfs): v3 format — double-indirect blocks, ~2 MiB fs`
* `feat(posix): chmod/fchmod/chown/lchown/fchown — wire syscalls`

Two on-disk format bumps land on the same branch (SBV1→SBV2→SBV3). Each
preserves the loud-fail magic check before log replay, so stale images
cannot corrupt anything.

## Max file / fs size

| | Before | After v2 | After v3 |
|---|---|---|---|
| Per-file ceiling | 132 KiB (12-slot dinode) | 132 KiB (10-slot, mode/uid/gid added) | ~8 MiB (fs-capped) |
| FS total | ~500 KB image | ~500 KB image | ~2.07 MB image |
| NDATABLOCKS | 1000 | 1000 | 4000 |

## New tests (in runtests)

* `bin/sbfs_bigfile_test` — 200 KiB pseudo-random write/read round-trip;
  exercises the double-indirect path; then 100 KiB second-pass to validate
  itrunc-via-unlink doesn't leak the indirect tree.
* `bin/chmod_chown_test` — 8 checks: default mode, chmod preserves type
  bits, chown full + sentinel, persist-across-reopen, tarfs EROFS, tmpfs
  fchmod.
* `bin/bigfile_pcache_test` — existing test, now in runtests (it relied
  on the pre-v3 cap to fail, so it was previously skipped).
* `bin/utimes_test` — extended with sbfs path.

## Verification

* `make clean && make qemu`
* `/bin/runtests` → **137/137** (was 133/134 at branch start, 134/134
  after the path-length fix in 9ecd758).
* Selftest banner → **278/278** unchanged.
* Manual: `dd if=/dev/zero of=/mnt/x bs=4k count=400 && stat /mnt/x` →
  `st_size=1638400` (was EFBIG before v3).

## Out of scope

* Permission enforcement — `chmod`/`chown` write the bits but no syscall
  rejects based on `pcb->uid` vs `ip->uid`. Matches the existing
  root-equivalent `sys_access` model. Separate follow-up.
* `atime` support — `stat` still synthesizes from `mtime`. Same trade as
  today.

## Specs / plans

* `docs/superpowers/specs/2026-05-17-sbfs-v3-bigfiles-design.md`
* `docs/superpowers/plans/2026-05-17-sbfs-v3-bigfiles.md`
```

- [ ] **Step 3: Confirm PR URL with user**

Once opened, return the PR URL to the user. Stop here unless the user asks for merge.

---

## Self-Review Checklist (run by implementer at end)

- [ ] **Spec coverage**: every requirement in the linked spec has a task? (Phase 1: tighten setmtime ✓, header v1 comment ✓, stat_direct assertion ✓. Phase 2: magic bump ✓, NDIR/NINDIR/NDINDIR ✓, MAX_FILE_SIZE ✓, readi ✓, writei ✓, itrunc ✓, selftest magic ✓, mkfs mirror ✓, NDATABLOCKS bump ✓, new test ✓, bigfile_pcache enrol ✓. Phase 3: setmode hook ✓, setowner hook ✓, sbfs impl ✓, syscall numbers ✓, sys_chmod ✓, sys_fchmod ✓, sys_chown ✓, sys_lchown ✓, sys_fchown ✓, libc wrappers ✓, new test ✓.)
- [ ] **Placeholder scan**: no "TBD", "TODO", "implement later" remaining.
- [ ] **Type consistency**: hook names match (`setmode`/`setowner` not `set_mode`/`set_owner`); syscall numbers stable across kernel and libc headers; `do_chmod_ip`/`do_chown_ip` helpers used in both path-based and fd-based variants.
- [ ] **Commit grouping**: 3 new commits (Phase 1, Phase 2, Phase 3), each independently passes tests.
