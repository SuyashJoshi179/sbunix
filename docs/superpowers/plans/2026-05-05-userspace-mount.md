# Userspace `mount` Command Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Match the professor's grader expectations by mounting filesystems from `/etc/rc` via a userspace `mount` command (instead of hardcoded kernel-side mounts at boot).

**Architecture:** Split each mountable filesystem into an `_init()` phase (data structures, log recovery, inode setup) called unconditionally at boot, and an `_attach(target)` phase that registers the fs root via `mount_fs()`. Kernel boot stops calling `mount_fs()`. A new `sys_mount(target, fstype)` syscall dispatches to the appropriate `_attach()` based on fstype. Selftests that depend on disk fs invoke `_attach()` helpers internally (no syscall path). A new `bin/mount` userspace binary parses `-t TYPE TARGET` and calls the syscall. `rootfs/etc/rc` is restored to invoke `mount -t proc proc /proc` and `mount -t disk virtio /mnt`. Mounts are idempotent (re-mount of same fs at same path returns 0) so selftest's pre-mount does not collide with rc's mount.

**Tech Stack:** RISC-V64 freestanding kernel (C11), QEMU virt machine, sbfs (in-tree fs), tarfs (read-only initramfs at `/`), procfs, bio cache, virtio-pci block driver, custom libc.

---

## Execution Progress Log

> **For a takeover agent:** This section is updated after each task is committed and pushed. To pick up where the previous agent left off, find the most recent commit on `feature/userspace-mount` matching the task list below, then resume from the next pending task. All commits are on remote `origin/feature/userspace-mount`.

| Task | Status | Commit (origin/feature/userspace-mount) |
|------|--------|------------------------------------------|
| Plan: initial draft | done | `03017d7` docs(plan): userspace mount command implementation plan |
| Plan: chat context appended | done | `7ec5c42` docs(plan): add chat context (Piazza Q&A, decision rationale, recon findings) |
| Task 1: procfs split | done | `215fc6b` fs(procfs): split init from attach (no mount in init) |
| Task 2: sbfs split | done | `4c23673` fs(sbfs): split mount into sbfs_init + sbfs_attach |
| Task 3: mount_fs idempotent | done | `999f637` fs(vfs): mount_fs idempotent + copy path into private storage |
| Task 4: kernel boot drops auto-mount | done | `9cd1d6c` kernel: stop auto-mounting procfs and sbfs at boot |
| Task 5: selftest pre-attach + /data→/mnt | done | `afbcc06` selftest: pre-attach procfs+sbfs, rename /data refs to /mnt |
| Task 6: tarfs ensure_dir → mnt+proc | done | `ecf8fa1` fs(tarfs): create /mnt and /proc mountpoints (replaces /data) |
| Task 7: SYS_mount syscall | done | included in this PR |
| Task 8: bin/mount userspace binary | done | included in this PR |
| Task 9: rootfs dirs + restore etc/rc | done | included in this PR |
| Task 10: rename /data→/mnt across 13 user files | done | `cca06ef` tests+libc: rename /data to /mnt to match new mountpoint |
| Task 11: end-to-end QEMU verify | done | `82fcd67` init+sh: run /etc/rc at boot via sh script mode |

### Notable deviations from the plan as written

- **Task 3**: plan specified bytewise compare on stored `path` pointer. Implementation went further and copied path into a `char path[MOUNT_PATH_MAX]` (64 bytes) inside the `mounts[]` table, because the previous design stored the caller's pointer — fine for kernel literals, but unsafe once `sys_mount` passes a stack-allocated `copyinstr` buffer. New constraint: `MOUNT_PATH_MAX = 64` ⇒ paths longer than 63 chars now return `-ENAMETOOLONG`. No real-world target hits this.
- **Task 5**: selftest now `#include <procfs.h>` (added to its include block) instead of inline `extern` declarations. `sbfs.h` already included. Cleaner than plan's fallback.
- **Task 6**: plan only changed `tarfs_ensure_dir("data")` → `tarfs_ensure_dir("mnt")`. Implementation also adds `tarfs_ensure_dir("proc")` defensively. `rootfs/proc/` exists empty in the source tree and is normally archived by GNU tar, but the explicit `ensure_dir` is cheap insurance.
- **Task 9 (fd_test)**: `bin/fd_test/fd_test.c` looked for the "SBUnix" substring in `/etc/rc` to verify file reads. Restoring prof's verbatim rc (which has no "SBUnix") broke the test. Switched the magic string to "mount", which is present in prof's rc and won't disappear (committed alongside Task 10's rename in `cca06ef`'s parent context — actually re-checking: it was edited in the same Task 10 work session, but logically belongs with Task 9's rc restore).
- **Task 11 (the big one)**: plan task 11 was framed as pure verification, but the e2e check exposed that nothing on the develop branch actually invokes `/etc/rc`. Master branch had no `bin/init` (kernel went straight to a shell that presumably auto-loaded rc); develop's `bin/init` exec'd `/bin/sh` with no args, and `/bin/sh` itself supported only `-c CMD`, not script files. So rc was a dead file before this change. Two new pieces of code:
  1. `bin/sh/sh.c`: `argc == 2 && argv[1][0] != '-'` → `run_script(path)`. Reads up to 4 KiB into a static buffer, walks line-by-line, copies each non-blank non-`#` line into `linebuf`, then `tokenize() + expand_globs() + run_line()`. Lines beginning with `exec ` are treated as no-ops because (a) we have no exec builtin and (b) rc's trailing `exec /bin/sh` would block init forever waiting on its child.
  2. `bin/init/init.c`: fork+exec `/bin/sh /etc/rc` early, before the test battery. Wait for completion. Mounts in rc hit `mount_fs` idempotency (selftest already pre-attached procfs+sbfs) and return 0. `procfs_attach`/`sbfs_attach` print on every successful return, so the boot console shows the mount lines twice — once from selftest, once from rc — which is harmless and intentional per Task 3's idempotency design.
  Also note: our `sh` doesn't expand `$0`, so rc's `echo Running $0` produces `Running $0` literally instead of `Running /etc/rc`. Out of scope for this plan; if grader requires literal expansion, edit `rc` to hardcode the path or implement `$0` substitution in `tokenize()`.

### Verification summary so far

After Tasks 1–6, `make build/kernel.elf` succeeds clean. QEMU boot shows:

```
sbfs: superblock ok (size=1051 nblocks=1000 ninodes=256 logstart=2)
========================================
[SELFTEST] Kernel self-tests starting
========================================
procfs: mounted /proc
sbfs: mounted /mnt
[SELFTEST] PASS  ...
```

Selftest completes without `FAIL` or `panic`. Userspace test runner picks up after; not yet verified end-to-end because that requires the userspace `mount` binary (Task 8) and `/data` → `/mnt` test path renames (Task 10).

After Task 11, full e2e via `make qemu` produces:

```
tarfs: mounted 109 inodes
devfs: mounted /dev
...
[SELFTEST] Results: 250 passed, 0 failed
init: starting
Running $0                  ← from rc's `echo Running $0` (no $0 expansion in our sh)
procfs: mounted /proc       ← rc's `mount -t proc proc /proc` (idempotent)
sbfs: mounted /mnt          ← rc's `mount -t disk virtio /mnt` (idempotent)
... 80 userspace tests, all PASS ...
init: 80/80 tests passed
Starting /bin/sh
sh>
```

Mount syscall, `/bin/mount`, idempotency, and `/data`→`/mnt` migration all verified end-to-end. Negative path also confirmed: at the shell, `mount -t bogus x /tmp` prints `mount: bogus on /tmp failed` and exits 1 (sys_mount returns `-EINVAL`).

### How to resume (takeover instructions)

```bash
git fetch origin
git checkout feature/userspace-mount
git pull --ff-only
# Read this section's table to find the next pending task.
# Then read the task body further down in this file and execute it
# step-by-step, committing + pushing per task.
```

Each task in the body below is self-contained with exact file paths, full code, and verification commands. Use `superpowers:executing-plans` (single session) or `superpowers:subagent-driven-development` (subagent per task) to drive.

---

## Conversation context (carry-over for any agent picking this up)

This plan came out of a discussion comparing `develop` (our work) to `master` (professor's evaluation baseline). Key facts established during that discussion — preserved here so a fresh agent has full context without scrolling chat history.

### Diff that started the discussion

`git diff master..develop -- rootfs/etc/rc`:

```diff
-#!/bin/sh
+# SBUnix init script
 echo Running $0
-mount -t proc proc /proc
-mount -t disk virtio /mnt
 exec /bin/sh
```

Two things on master we lost: `mount -t proc proc /proc` and `mount -t disk virtio /mnt`. Mount target on master is `/mnt`, on develop sbfs is mounted at `/data`.

### Current develop state (verified by recon)

- `kernel/kernel.c` calls `procfs_init()` (which internally `mount_fs("/proc", &proc_root_inode)`) and `mount_fs("/data", sbfs_root)`. Both happen at boot, before any userspace runs.
- No `mount` syscall: `grep -nE "SYS_mount|sys_mount" kernel/include/syscall.h kernel/syscall.c` returns nothing. Existing syscall numbers reach `SYS_meminfo 111` and signals up to `SYS_setpgid 95`. #83 is free (between `SYS_nanosleep 82` and `SYS_kill 90`).
- No `bin/mount/` directory. No `mount` userspace binary anywhere.
- `rootfs/proc/` exists empty. `rootfs/mnt/` does not exist. `rootfs/data/` does not exist (the dir is created at boot by `tarfs_ensure_dir("data")` in `kernel/fs/tarfs.c:343`).
- 13 files reference `/data` literals: `bin/{pagecache_stress,link_test,rename_test,timestamp_test,sbfs_basic_test,usertests,bigfile_pcache_test,mkdir_test,truncate_mmap_test,pagecache_test,mmap_share_test,header_test}/*.c` and `libc/mntent.c`.
- Selftest runs **before** init/shell/rc. `kernel/selftest.c` does `namei("/data", ...)` directly. Any change that defers sbfs mounting to userspace will break selftests unless they pre-mount themselves.

### Piazza posts that drove the decision (verbatim)

**Post 1 — "creating dev, proc and mnt subdirectories in rootfs"**

Student: "Can we create the dev, proc and mnt subdirectories in the rootfs directory so that devfs and procfs have somewhere to latch onto so they can be mounted? Since tarfs is supposed to be readonly I assume we aren't supposed to be able to create new inodes for it. Besides when mounting on Linux you usually need to have that directory created anyways. Also for all these filesystems should we automount them on boot? Should we create an fstab file in etc, so that the init process can mount them or leave it for the agent?"

Prof: "yes, you should create proc/ and mnt/ in rootfs/ (you're welcome to create dev/ also, but it's not required). it also contains etc/rc which should mount the filesystems on boot."

**Post 2 — "Disk filesystems"**

Student: "Can we change mkfs or add stuff to the tools directory to set up the disk with a filesystem so that it can be mounted later? For mounting the disk, I saw on an earlier thread that we should allow mounting from user space - is that necessary? If it is, should we implement it similar to Linux? Allow mounting a device on /dev/xyz to a directory?"

Prof: "You should change mkfs to correspond to your own filesystem implementation. Yes, you should support a 'mount' command which mounts on a /mnt directory. You do not need to support a /dev filesystem, specifying the filesystem type (e.g., 'mount -t proc /proc' and 'mount -t disk /mnt') is sufficient."

### Decision taken — Option B (full mount syscall)

Three options were weighed during chat:

- **Option A (rejected):** keep kernel-side auto-mount, change path `/data` → `/mnt`, ship `bin/mount` as a no-op stub that exits 0 so rc doesn't fail.
- **Option B (chosen):** real `sys_mount(target, fstype)` + real `bin/mount`. rc actually drives mounting. Idempotent so the kernel selftest can pre-attach without conflicting.
- **Option C (out of scope):** full Linux-style mount with source/dev (e.g. `/dev/vda`). Prof explicitly said source isn't needed; type alone is sufficient. Skipped.

Option B was chosen because Post 2's prof answer is unambiguous that `mount` must be a userspace command that actually mounts. A stubbed `mount` would fake the rc line but not satisfy "support a 'mount' command which mounts on /mnt".

### Tarfs is read-only — why creating the mountpoint dirs is non-trivial

Tarfs is bundled into the kernel ELF as a read-only blob (`build/tarfs.o` from `tar cf build/rootfs.tar -C build/rootfs .`). Files/dirs that are not in the tar at link time cannot be created at runtime. Hence:

- `rootfs/proc/` and `rootfs/mnt/` must exist in the source tree before tarfs is built.
- `tarfs_ensure_dir("mnt")` in `kernel/fs/tarfs.c` (currently `tarfs_ensure_dir("data")`) creates the in-memory dir even if the tar didn't carry it, but the source-tree dir is still expected by prof and conventional.
- Tar archives ignore truly empty directories on some configurations, so `.keep` placeholder files are added to guarantee inclusion. Once procfs/sbfs mount on top, the placeholders are shadowed by `mount_child` traversal and never user-visible.

### Selftest interaction (the subtle one)

Selftest runs in kernel context at boot, well before any userspace. It currently does `namei("/data", ...)` for sbfs tests and `namei("/proc/...", ...)` for procfs tests. If we strip kernel auto-mount, those calls return -ENOENT.

Two options were considered:

- **(rejected)** move sbfs/procfs tests out of selftest into userspace tests so they only run after rc has mounted.
- **(chosen)** selftest calls `procfs_attach()` and `sbfs_attach()` itself before running its tests. rc later calls `mount` again via the syscall; the second call hits idempotency in `mount_fs` and returns 0 silently. Net effect: tests pass, rc behaves as prof expects, no double-mount damage.

The idempotency check has to be byte-wise (`strcmp`-style on `path`) not pointer-equality, because the second call comes from `copyinstr`'d userspace strings whose pointers differ from the kernel literal `"/proc"` used by selftest. The plan's Task 3 implements this correctly.

### Ranges that informed the plan

- `kernel/fs/procfs.c:650-700` — `procfs_init` body. The two lines `mount_fs("/proc", &proc_root_inode); printk(...)` are extracted to a new `procfs_attach`.
- `kernel/fs/sbfs.c:958-983` — `sbfs_mount` becomes `sbfs_init` + `sbfs_attach`. `sbfs_ready` flag is reused; `sbfs_attach` refuses to attach if init failed (`-ENODEV`).
- `kernel/fs/vfs.c:26-53` — `mount_fs`. Idempotency loop added at top.
- `kernel/kernel.c:48,57-69` — boot sequence. `procfs_init()` keeps its name (semantics changed), `sbfs_mount(...)` block replaced with `sbfs_init()` only.
- `kernel/syscall.c:~1340` — dispatch switch. New `case SYS_mount`.
- `kernel/include/syscall.h:49` — slot #83 chosen because it sits in the time-block gap and is currently unused.

### What this plan does NOT do (deferred / out-of-scope)

- `umount` syscall. Prof did not ask for it. Mounts in this kernel are permanent for the life of the process.
- `/dev` filesystem-style mounting. Prof explicitly said not required.
- Source argument support. `mount -t TYPE SOURCE TARGET` is accepted at the CLI layer (so `mount -t disk virtio /mnt` parses), but `SOURCE` is discarded — `mount(target, fstype)` is the syscall surface.
- Multiple sbfs instances or multiple disks. One virtio block device, one sbfs.
- `mkfs` changes. Prof's Post 2 says "change mkfs to correspond to your own filesystem implementation"; ours already does. If grading reveals it doesn't, follow up separately.
- `fstab`. Student asked, prof did not require — rc invokes `mount` directly.

### Quick verification checklist for reviewer

- [ ] Does the plan keep selftest passing? (Tasks 5 + 3 together: selftest pre-attaches, mount_fs idempotent.)
- [ ] Does rc still work the way prof wrote it? (Task 9 restores it verbatim; `bin/mount` accepts the 4-arg form.)
- [ ] Are `/proc/` and `/mnt/` real directories in the tarball? (Task 9 `.keep` files; Task 6 `tarfs_ensure_dir("mnt")`.)
- [ ] Are all `/data` literals migrated? (Task 10 uses an exact file list verified by `grep -rln '/data' bin/ libc/`.)
- [ ] Is `SYS_mount` slot #83 actually free? (Confirmed during recon; gap between 82 and 90.)

---

## Background — Why this change

Professor's master `rootfs/etc/rc` reads:

```sh
#!/bin/sh
echo Running $0
mount -t proc proc /proc
mount -t disk virtio /mnt
exec /bin/sh
```

Two Piazza answers from prof confirm the requirement:
1. "you should create proc/ and mnt/ in rootfs/ … etc/rc should mount the filesystems on boot."
2. "support a 'mount' command which mounts on /mnt … specifying the filesystem type (e.g., 'mount -t proc /proc' and 'mount -t disk /mnt') is sufficient."

Develop branch currently:
- Pre-mounts procfs at `/proc` inside `procfs_init()` (kernel boot).
- Pre-mounts sbfs at `/data` inside `kernel/kernel.c` (kernel boot).
- Has no `mount` syscall and no `bin/mount` binary.
- `rootfs/etc/rc` was reduced to `echo Running … exec /bin/sh`.

Required changes are scoped to:

- Kernel: new `sys_mount` syscall (#83 free), split init/attach in procfs and sbfs, idempotency in `mount_fs`.
- Userspace: new `bin/mount` C program.
- Rootfs: add `mnt/` (and confirm `proc/`) mountpoint dirs, restore `etc/rc`.
- Tarfs init: stop creating `data/`, start creating `mnt/`.
- Tests: rewrite `/data/...` paths to `/mnt/...` across `bin/*_test`, `libc/mntent.c`, kernel selftest.

Path resolution and `mount_child` semantics are unchanged.

---

## File Structure

| File | Status | Responsibility |
|------|--------|----------------|
| `kernel/include/syscall.h` | modify | add `SYS_mount` (#83) |
| `kernel/include/vfs.h` | modify | add `vfs_already_mounted_at(path, root)` helper decl |
| `kernel/fs/vfs.c` | modify | make `mount_fs` idempotent (same root at same path = success) |
| `kernel/include/procfs.h` (or extend `procfs.c`) | modify | split `procfs_init()` (no mount) + new `procfs_attach(const char *target)` |
| `kernel/fs/procfs.c` | modify | implement split |
| `kernel/include/sbfs.h` | modify | rename current `sbfs_mount` → `sbfs_init`; new `sbfs_attach(const char *target)` |
| `kernel/fs/sbfs.c` | modify | implement split |
| `kernel/kernel.c` | modify | call `procfs_init`/`sbfs_init` only; remove `mount_fs("/data", …)` |
| `kernel/syscall.c` | modify | add `sys_mount` dispatch |
| `kernel/selftest.c` | modify | call `procfs_attach("/proc")` and `sbfs_attach("/mnt")` before sbfs/proc tests; flip `/data` → `/mnt` everywhere |
| `kernel/fs/tarfs.c` | modify | drop `tarfs_ensure_dir("data")`, add `tarfs_ensure_dir("mnt")` |
| `bin/mount/mount.c` | create | userspace mount binary; parses `-t TYPE TARGET` |
| `rootfs/etc/rc` | modify | restore prof's two `mount` invocations |
| `rootfs/mnt/.keep` | create | ensure `/mnt/` exists in rootfs (tar archives include the dir) |
| `rootfs/proc/.keep` | create | ensure `/proc/` exists (already empty dir; placeholder file makes tar include it) |
| `libc/mntent.c` | modify | replace `/data` → `/mnt` |
| `bin/pagecache_stress/pagecache_stress.c` | modify | `/data/...` → `/mnt/...` |
| `bin/link_test/link_test.c` | modify | `/data/...` → `/mnt/...` |
| `bin/rename_test/rename_test.c` | modify | `/data/...` → `/mnt/...` |
| `bin/timestamp_test/timestamp_test.c` | modify | `/data/...` → `/mnt/...` |
| `bin/sbfs_basic_test/sbfs_basic_test.c` | modify | `/data/...` → `/mnt/...` |
| `bin/usertests/usertests.c` | modify | `/data/...` → `/mnt/...` |
| `bin/bigfile_pcache_test/bigfile_pcache_test.c` | modify | `/data/...` → `/mnt/...` |
| `bin/mkdir_test/mkdir_test.c` | modify | `/data/...` → `/mnt/...` |
| `bin/truncate_mmap_test/truncate_mmap_test.c` | modify | `/data/...` → `/mnt/...` |
| `bin/pagecache_test/pagecache_test.c` | modify | `/data/...` → `/mnt/...` |
| `bin/mmap_share_test/mmap_share_test.c` | modify | `/data/...` → `/mnt/...` |
| `bin/header_test/header_test.c` | modify | `/data/...` → `/mnt/...` |

---

## Order of tasks

1. Kernel split: procfs init/attach.
2. Kernel split: sbfs init/attach.
3. `mount_fs` idempotency.
4. Boot rewires (no auto-mount).
5. Selftest internal mount + `/data` → `/mnt` rename.
6. Tarfs `data` → `mnt` mountpoint.
7. `sys_mount` syscall.
8. Userspace `bin/mount`.
9. Rootfs dir + rc restoration.
10. Userspace test path renames.
11. End-to-end QEMU verification.

Each task ends with a commit. Commits are small and bisectable. Tests added per task where applicable; the selftest harness inside the kernel is the primary correctness gate, with QEMU smoke runs at the integration boundary.

---

### Task 1: Split `procfs_init` into init + attach

**Files:**
- Modify: `kernel/fs/procfs.c` (function `procfs_init` near line 650; near end of file)
- Modify: `kernel/include/procfs.h` (export new symbol)

- [ ] **Step 1: Read the current `procfs_init`**

Run: `grep -n "procfs_init\|mount_fs" kernel/fs/procfs.c`
Expected: One definition of `procfs_init` containing `mount_fs("/proc", &proc_root_inode);`.

- [ ] **Step 2: Modify `kernel/fs/procfs.c` — remove the `mount_fs` line from `procfs_init`**

Locate the two-line block at the end of `procfs_init`:

```c
    mount_fs("/proc", &proc_root_inode);
    printk("procfs: mounted /proc\n");
```

Delete those two lines.

- [ ] **Step 3: Add `procfs_attach` at the bottom of `kernel/fs/procfs.c`**

Append:

```c
/* Register the procfs root at `target`. Returns 0 on success, negative
 * errno on failure. Idempotent when called twice with the same target
 * (mount_fs already detects same-root re-mount). */
int procfs_attach(const char *target) {
    int rc = mount_fs(target, &proc_root_inode);
    if (rc == 0) printk("procfs: mounted %s\n", target);
    return rc;
}
```

- [ ] **Step 4: Declare `procfs_attach` in `kernel/include/procfs.h`**

Add at the end of the header (before any closing `#endif`):

```c
int procfs_attach(const char *target);
```

- [ ] **Step 5: Build kernel to confirm no compile errors**

Run: `make build/kernel.elf`
Expected: success, no errors.

- [ ] **Step 6: Commit**

```bash
git add kernel/fs/procfs.c kernel/include/procfs.h
git commit -m "fs(procfs): split init from attach (no mount in init)"
```

---

### Task 2: Split `sbfs_mount` into `sbfs_init` + `sbfs_attach`

**Files:**
- Modify: `kernel/fs/sbfs.c` (function `sbfs_mount` near line 958)
- Modify: `kernel/include/sbfs.h` (line 78)

- [ ] **Step 1: Find current declaration**

Run: `grep -nE "sbfs_mount|sbfs_ready" kernel/fs/sbfs.c kernel/include/sbfs.h`
Expected: one prototype + one definition; `sbfs_ready` is a static int.

- [ ] **Step 2: Rename `sbfs_mount` → `sbfs_init` in `kernel/fs/sbfs.c`**

Change function header from:

```c
struct inode *sbfs_mount(void) {
```

to:

```c
int sbfs_init(void) {
```

Replace the trailing `return sbfs_iget(SBFS_ROOTINUM);` with:

```c
    return 0;
```

Replace `return 0;` (the bad-magic path) — current body is:

```c
    if (sb.magic != SBFS_MAGIC) {
        printk("sbfs: bad magic 0x%x (want 0x%x)\n", sb.magic, SBFS_MAGIC);
        return 0;
    }
```

with:

```c
    if (sb.magic != SBFS_MAGIC) {
        printk("sbfs: bad magic 0x%x (want 0x%x)\n", sb.magic, SBFS_MAGIC);
        return -EINVAL;
    }
```

- [ ] **Step 3: Add `sbfs_attach` directly below `sbfs_init`**

Append:

```c
/* Register sbfs root at `target`. Must be called after sbfs_init(). */
int sbfs_attach(const char *target) {
    if (!sbfs_ready) return -ENODEV;
    struct inode *root = sbfs_iget(SBFS_ROOTINUM);
    if (!root) return -ENOMEM;
    int rc = mount_fs(target, root);
    if (rc < 0) {
        inode_put(root);
        return rc;
    }
    printk("sbfs: mounted %s\n", target);
    return 0;
}
```

Make sure `<errno.h>` is already included (it is — verify with `grep -n errno.h kernel/fs/sbfs.c`).

- [ ] **Step 4: Update `kernel/include/sbfs.h`**

Replace:

```c
struct inode *sbfs_mount(void);
```

with:

```c
int sbfs_init(void);
int sbfs_attach(const char *target);
```

- [ ] **Step 5: Build kernel — expect kernel.c link error**

Run: `make build/kernel.elf`
Expected: link/compile error in `kernel/kernel.c` referencing old `sbfs_mount`. (Will be fixed in Task 4.)

- [ ] **Step 6: Commit**

```bash
git add kernel/fs/sbfs.c kernel/include/sbfs.h
git commit -m "fs(sbfs): split mount into sbfs_init + sbfs_attach"
```

---

### Task 3: Make `mount_fs` idempotent for same-root re-mount

**Files:**
- Modify: `kernel/fs/vfs.c:26-53`

Rationale: rc will call `mount` after the kernel selftest already attached procfs and sbfs. A second `mount_fs` of the same root at the same path must return 0, not -ENOMEM. A different root at the same path must still fail (treat as `-EBUSY`).

- [ ] **Step 1: Read current `mount_fs`**

Run: `sed -n '20,55p' kernel/fs/vfs.c`
Expected: function as it appears in the codebase (no idempotency check).

- [ ] **Step 2: Insert duplicate-mount detection at the top of `mount_fs`**

Replace the body of `mount_fs` with:

```c
int mount_fs(const char *path, struct inode *root) {
    /* Idempotency: if the exact (path, root) is already mounted, return 0.
     * If the path is mounted with a *different* root, fail with -EBUSY. */
    for (int i = 0; i < nmounts; i++) {
        if (mounts[i].path && path) {
            const char *a = mounts[i].path, *b = path;
            while (*a && *b && *a == *b) { a++; b++; }
            if (*a == 0 && *b == 0) {
                return mounts[i].root == root ? 0 : -EBUSY;
            }
        }
    }

    if (nmounts >= NMOUNT) return -ENOMEM;
    mounts[nmounts].path = path;
    mounts[nmounts].root = root;
    nmounts++;

    if (path[0] == '/' && path[1] == '\0') {
        return 0;
    }

    struct inode *mp;
    int rc = namei(path, &mp);
    if (rc < 0) {
        printk("mount_fs: can't resolve '%s': %d\n", path, rc);
        nmounts--;
        return rc;
    }
    mp->mount_child = root;
    root->mount_parent = mp;
    inode_put(mp);
    return 0;
}
```

Make sure `<errno.h>` is included (search: `grep -n errno.h kernel/fs/vfs.c`); if not, add `#include <errno.h>`.

- [ ] **Step 3: Build to confirm no compile errors in vfs.c**

Run: `make build/kernel/fs/vfs.o`
Expected: builds clean.

- [ ] **Step 4: Commit**

```bash
git add kernel/fs/vfs.c
git commit -m "fs(vfs): mount_fs idempotent for identical (path, root)"
```

---

### Task 4: Rewire kernel boot — no auto-mount of procfs or sbfs

**Files:**
- Modify: `kernel/kernel.c:48` and `kernel/kernel.c:57-69`

- [ ] **Step 1: Read current boot block**

Run: `sed -n '28,75p' kernel/kernel.c`
Expected: shows `procfs_init();` at ~line 48 and the sbfs `mount_fs("/data", ...)` block at ~57-69.

- [ ] **Step 2: Replace the sbfs block with a call to `sbfs_init`**

Locate the block (lines ~57-69):

```c
    // Mount sbfs at /data.
    struct inode *sbfs_root = sbfs_mount();
    if (sbfs_root) {
        int rc = mount_fs("/data", sbfs_root);
        if (rc < 0) {
            printk("kernel: mount /data failed (%d)\n", rc);
            inode_put(sbfs_root);   /* mount failed; release our ref */
        } else {
            printk("kernel: /data mounted (sbfs v1)\n");
            /* mount_child holds the ref — do NOT inode_put here */
        }
    } else {
        printk("kernel: sbfs_mount failed — /data unavailable\n");
    }
```

Replace with:

```c
    // Initialise sbfs in-memory state and replay the log. Attach is
    // deferred until userspace `mount -t disk … /mnt` (or selftest).
    if (sbfs_init() < 0)
        printk("kernel: sbfs_init failed — /mnt unavailable\n");
```

`procfs_init();` at the earlier line stays as-is — the function no longer mounts since Task 1.

- [ ] **Step 3: Build the full kernel**

Run: `make build/kernel.elf`
Expected: builds clean. (Selftest will fail at runtime until Task 5; that is fine for now.)

- [ ] **Step 4: Commit**

```bash
git add kernel/kernel.c
git commit -m "kernel: stop auto-mounting procfs and sbfs at boot"
```

---

### Task 5: Selftest pre-mount + `/data` → `/mnt` rename

The kernel selftest runs *before* `init` and therefore before rc. It must call `procfs_attach("/proc")` and `sbfs_attach("/mnt")` itself, and tests that hardcode `/data` must use `/mnt`.

**Files:**
- Modify: `kernel/selftest.c` (around lines 258, 485-510, and the entry point near `selftest_run`)

- [ ] **Step 1: Locate the selftest entry point**

Run: `grep -n "void selftest_run\b\|namei.*'/data'\|namei.*\"/dev/console\"" kernel/selftest.c`
Expected: `selftest_run` at line ~918, `/data` references near 485-510, `/dev/console` namei near 259.

- [ ] **Step 2: Add internal mount calls at the top of `selftest_run`**

Insert immediately inside `selftest_run` (before any `st_check` calls):

```c
    extern int procfs_attach(const char *target);
    extern int sbfs_attach(const char *target);
    if (procfs_attach("/proc") < 0)
        printk("[SELFTEST] WARN: procfs_attach('/proc') failed\n");
    if (sbfs_attach("/mnt") < 0)
        printk("[SELFTEST] WARN: sbfs_attach('/mnt') failed\n");
```

Use `extern` declarations inline rather than including `procfs.h`/`sbfs.h` here — the existing file already prefers local externs in places. (Verify: `grep -n "^#include" kernel/selftest.c | head`. If `procfs.h` and `sbfs.h` are already included, drop the `extern`s and call directly.)

- [ ] **Step 3: Replace `/data` with `/mnt` in selftest assertions and printk strings**

Run: `sed -i 's|/data|/mnt|g' kernel/selftest.c`

Then verify:

Run: `grep -n '/data' kernel/selftest.c`
Expected: no matches.

- [ ] **Step 4: Build and run QEMU; verify `init: NN/NN tests passed`**

Run: `make qemu`
Expected: kernel boots, selftest runs, output line `init: <N>/<N> tests passed` appears with no failures (any pre-existing flake count is acceptable but no `/mnt` regressions). Hit `Ctrl-A x` to exit QEMU.

- [ ] **Step 5: Commit**

```bash
git add kernel/selftest.c
git commit -m "selftest: pre-attach procfs+sbfs, rename /data refs to /mnt"
```

---

### Task 6: Tarfs — replace `data` mountpoint with `mnt`

**Files:**
- Modify: `kernel/fs/tarfs.c:343`

- [ ] **Step 1: Locate the line**

Run: `grep -n "tarfs_ensure_dir" kernel/fs/tarfs.c`
Expected: 4 calls in `tarfs_init`: `dev`, `bin`, `etc`, `data`.

- [ ] **Step 2: Edit line 343**

Change:

```c
    tarfs_ensure_dir("data");
```

to:

```c
    tarfs_ensure_dir("mnt");
```

- [ ] **Step 3: Build**

Run: `make build/kernel.elf`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add kernel/fs/tarfs.c
git commit -m "fs(tarfs): create /mnt mountpoint instead of /data"
```

---

### Task 7: `sys_mount(target, fstype)` syscall

**Files:**
- Modify: `kernel/include/syscall.h` (add `SYS_mount` near time/signal block; #83 is free)
- Modify: `kernel/syscall.c` (add `sys_mount`, dispatch case)

- [ ] **Step 1: Add the syscall number**

In `kernel/include/syscall.h`, after the `SYS_nanosleep 82` line, add:

```c
#define SYS_mount         83   // (const char *target, const char *fstype)
```

- [ ] **Step 2: Add `sys_mount` body in `kernel/syscall.c`**

Add a new static function near the other fs syscalls (before the dispatch switch). Pick a location next to `sys_chdir`/`sys_mkdir`. The function:

```c
/* sys_mount(target, fstype) — userspace mount.
 *
 * Supported fstypes: "proc" (procfs), "disk" (sbfs).
 * `source` argument from the userspace `mount -t TYPE SOURCE TARGET`
 * command line is ignored (we don't have a /dev fs); fstype alone
 * selects the backing.
 *
 * Path is copied via copyinstr to a kernel buffer; fstype likewise.
 * Returns 0 on success, negative errno otherwise. */
static int64_t sys_mount(const char *u_target, const char *u_fstype) {
    char target[64];
    char fstype[16];
    if (copyinstr(target, u_target, sizeof(target)) < 0) return -EFAULT;
    if (copyinstr(fstype, u_fstype, sizeof(fstype)) < 0) return -EFAULT;

    extern int procfs_attach(const char *target);
    extern int sbfs_attach(const char *target);

    if (strcmp(fstype, "proc") == 0)
        return procfs_attach(target);
    if (strcmp(fstype, "disk") == 0)
        return sbfs_attach(target);
    return -EINVAL;
}
```

Verify `copyinstr` and `strcmp` are already available in this translation unit:

Run: `grep -nE "copyinstr\b|^#include.*string\.h" kernel/syscall.c | head`
Expected: both present. If `string.h` not included, add `#include <string.h>` at the top.

- [ ] **Step 3: Wire into the dispatch switch**

In `kernel/syscall.c`, locate the dispatch switch (around line 1340). Add a new case alongside the existing fs cases:

```c
        case SYS_mount:
            return sys_mount((const char *)trapframe[TF_A0],
                             (const char *)trapframe[TF_A1]);
```

- [ ] **Step 4: Build kernel**

Run: `make build/kernel.elf`
Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add kernel/include/syscall.h kernel/syscall.c
git commit -m "syscall: add SYS_mount dispatching to procfs/sbfs attach"
```

---

### Task 8: Userspace `bin/mount`

**Files:**
- Create: `bin/mount/mount.c`

The Makefile auto-discovers `bin/*/*.c` (see `Makefile:14`), so creating this directory is sufficient — no Makefile edit needed.

- [ ] **Step 1: Read existing libc syscall plumbing for an example**

Run: `grep -rn "SYS_chdir\|sys_chdir" libc/ | head`
Expected: a libc wrapper that issues a syscall via inline asm or a helper macro. Use the same idiom for `mount`.

- [ ] **Step 2: Add the syscall wrapper**

Find the file that defines `chdir` in libc:

Run: `grep -rln "int chdir" libc/`

In that same file (or an adjacent one already exporting fs syscalls), add:

```c
int mount(const char *target, const char *fstype) {
    return (int)__syscall2(SYS_mount, (long)target, (long)fstype);
}
```

…using whichever helper macro libc actually uses (`__syscall2`, `_syscall2`, etc. — match the existing pattern shown by the grep above). Add the prototype to `libc/include/sys/mount.h` (create if absent):

```c
#ifndef _SYS_MOUNT_H
#define _SYS_MOUNT_H
int mount(const char *target, const char *fstype);
#endif
```

- [ ] **Step 3: Implement `bin/mount/mount.c`**

Create the file with:

```c
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: mount -t TYPE SOURCE TARGET\n");
}

int main(int argc, char **argv) {
    /* Accept both "mount -t TYPE SOURCE TARGET" (prof's rc) and
     * "mount -t TYPE TARGET". SOURCE is ignored. */
    if (argc < 4 || strcmp(argv[1], "-t") != 0) {
        usage();
        return 2;
    }
    const char *fstype = argv[2];
    const char *target = argv[argc - 1];   /* last positional arg */

    if (mount(target, fstype) < 0) {
        fprintf(stderr, "mount: %s on %s failed\n", fstype, target);
        return 1;
    }
    return 0;
}
```

- [ ] **Step 4: Build the binary**

Run: `make build/rootfs/bin/mount`
Expected: produced under `build/rootfs/bin/mount`.

- [ ] **Step 5: Commit**

```bash
git add bin/mount/mount.c libc/include/sys/mount.h libc/<file_with_chdir>.c
git commit -m "bin(mount): userspace mount(8) calling SYS_mount"
```

(Replace `<file_with_chdir>.c` with the actual file you edited in Step 2.)

---

### Task 9: Rootfs mountpoints + restore `etc/rc`

**Files:**
- Create: `rootfs/mnt/.keep`
- Create: `rootfs/proc/.keep`
- Modify: `rootfs/etc/rc`

`tar` only includes directories that have entries (or empty dirs explicitly archived). The `Makefile:76` line `tar cf build/rootfs.tar -C build/rootfs .` will include empty dirs since the build copies `rootfs/.` first; nevertheless, a `.keep` placeholder is the conventional, robust signal.

- [ ] **Step 1: Create placeholders**

Run:
```bash
mkdir -p rootfs/mnt rootfs/proc
: > rootfs/mnt/.keep
: > rootfs/proc/.keep
```

- [ ] **Step 2: Restore `rootfs/etc/rc`**

Replace the file contents with:

```sh
#!/bin/sh
echo Running $0
mount -t proc proc /proc
mount -t disk virtio /mnt
exec /bin/sh
```

(Use `Write` tool — do not use `echo >` from bash per CLAUDE.md.)

- [ ] **Step 3: Build the rootfs**

Run: `make build/tarfs.o`
Expected: clean build, `build/rootfs.tar` updated.

- [ ] **Step 4: Boot and verify rc runs the mounts**

Run: `make qemu`
Expected: console shows `Running /etc/rc`, no `mount: … failed` lines, and `sh>` prompt appears. (Mounts succeed even though selftest already mounted them — Task 3 made `mount_fs` idempotent.)

In the shell, run:
```
ls /proc
ls /mnt
```
Expected: `/proc` lists procfs entries; `/mnt` lists sbfs root contents (or is empty if blank). Hit `Ctrl-A x` to exit.

- [ ] **Step 5: Commit**

```bash
git add rootfs/mnt/.keep rootfs/proc/.keep rootfs/etc/rc
git commit -m "rootfs: add /mnt /proc mountpoints, restore mount calls in rc"
```

---

### Task 10: Userspace test path renames `/data` → `/mnt`

**Files:** twelve userspace test files plus `libc/mntent.c` (full list below).

- [ ] **Step 1: Locate every `/data` reference outside docs and `.original.md`**

Run:
```bash
grep -rln '/data' bin/ libc/ rootfs/ | grep -v '.original.md'
```
Expected output (exact set):
```
bin/pagecache_stress/pagecache_stress.c
bin/link_test/link_test.c
bin/rename_test/rename_test.c
bin/timestamp_test/timestamp_test.c
bin/sbfs_basic_test/sbfs_basic_test.c
bin/usertests/usertests.c
bin/bigfile_pcache_test/bigfile_pcache_test.c
bin/mkdir_test/mkdir_test.c
bin/truncate_mmap_test/truncate_mmap_test.c
bin/pagecache_test/pagecache_test.c
bin/mmap_share_test/mmap_share_test.c
bin/header_test/header_test.c
libc/mntent.c
```

- [ ] **Step 2: For each file, replace every `/data` with `/mnt`**

Use the `Edit` tool with `replace_all: true` per file. Example for `libc/mntent.c`:

```
Edit(file_path="libc/mntent.c", old_string="/data", new_string="/mnt", replace_all=true)
```

Repeat for each of the 13 files. (Do not use `sed -i` per CLAUDE.md.)

Also update the comment on `libc/mntent.c:6`:

Replace:
```c
/* Hardcoded mount table: tarfs at / and sbfs at /data. We dont have
```
with:
```c
/* Hardcoded mount table: tarfs at / and sbfs at /mnt. We dont have
```

- [ ] **Step 3: Verify no remaining `/data` references**

Run:
```bash
grep -rln '/data' bin/ libc/ rootfs/ | grep -v '.original.md'
```
Expected: empty output.

- [ ] **Step 4: Build all userspace tests**

Run: `make`
Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add bin/ libc/mntent.c
git commit -m "tests+libc: rename /data to /mnt to match new mountpoint"
```

---

### Task 11: End-to-end QEMU verification

- [ ] **Step 1: Full clean build**

Run: `make clean && make`
Expected: builds clean.

- [ ] **Step 2: Boot QEMU and exercise the shell**

Run: `make qemu`

Expected console behavior:
1. `Booting SBUnix` …
2. `procfs: …`, `sbfs: mounted (size=…)` from `sbfs_init`.
3. Selftest output ending with `init: <N>/<N> tests passed` (no failures).
4. `Running /etc/rc`.
5. `procfs: mounted /proc` and `sbfs: mounted /mnt` printed *again* with no error (idempotent re-mount returns 0 silently — actually procfs_attach prints; that is fine).
6. `sh>` prompt.

In the shell, run:
- `ls /` → includes `mnt`, `proc`, `bin`, `etc`, `dev`.
- `ls /proc` → procfs entries (`uptime`, `meminfo`, `version`, `cpuinfo`, `self`).
- `ls /mnt` → sbfs root contents.
- `cat /proc/version` → version string.
- `echo hi > /mnt/test.txt && cat /mnt/test.txt` → writes and reads back `hi` (sbfs is rw).
- `cat /mnt/../mnt/test.txt` (sanity check `..` works across mount).

Hit `Ctrl-A x` to exit.

- [ ] **Step 3: Run usertests**

Inside QEMU shell: `usertests`
Expected: all assertions pass; any pre-existing pre-mount-related skips no longer relevant.

Exit QEMU.

- [ ] **Step 4: Commit nothing — this is verification only**

If everything passes, no commit. If something fails, fix it in a new commit referencing the specific failing case.

---

## Self-Review checklist

- [x] **Spec coverage** — every prof requirement (`/proc/`, `/mnt/` dirs in rootfs; rc mounts on boot; `mount -t TYPE TARGET`; `proc` and `disk` types) maps to Tasks 1-10.
- [x] **Placeholder scan** — no `TBD`, no "implement appropriate", no "similar to Task N". One non-literal token: `<file_with_chdir>.c` in Task 8 step 5 — explicit instruction tells the engineer to substitute the file located in Task 8 Step 2 grep output. This is acceptable because the engineer cannot know it without running the grep.
- [x] **Type consistency** — `procfs_attach(const char *target)` and `sbfs_attach(const char *target)` use the same signature in declarations (procfs.h, sbfs.h), definitions (procfs.c, sbfs.c), externs (selftest.c, syscall.c), and call sites. `sbfs_init(void)` returns `int` consistently.
- [x] **Risk: existing kernel boot prints `kernel: /data mounted (sbfs v1)`** — Task 4 deletes that block; replacement message comes from `sbfs_init`/`sbfs_attach`. No grader script greps for the old message (verified via `grep -r '/data mounted' .` returning only the source file we are deleting).
- [x] **Risk: `mount_fs` already-mounted detection uses string compare on stored `path` pointer** — `mounts[i].path` is whatever pointer the caller passed; for selftest pre-attach the literal `"/proc"` pointer is the same .rodata address as a later kernel call, but for a *different* call site (syscall path, dynamically copied buffer) the pointers differ. The new code does a *byte-wise* compare (`while (*a && *b && *a == *b)`), not pointer compare, so this is correct regardless of pointer identity.
- [x] **Risk: `/proc/.keep` and `/mnt/.keep` placeholder files leak into the mounted fs view** — these live in tarfs at `/proc/.keep` and `/mnt/.keep`; once procfs/sbfs are mounted on top, the path resolver crosses `mount_child` and the `.keep` files are shadowed. Confirmed by `kernel/fs/vfs.c` mount semantics. Pre-mount listings (only via internal kernel paths before selftest) would show the placeholders, but no production code path lists those directories before mount.

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-05-05-userspace-mount.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — fresh subagent per task, two-stage review per task, fastest iteration.

**2. Inline Execution** — execute tasks in this session via `superpowers:executing-plans` with checkpoints.

**Which approach?**
