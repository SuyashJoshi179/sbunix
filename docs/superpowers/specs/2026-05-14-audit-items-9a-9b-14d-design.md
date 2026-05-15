# Design: remaining audit one-liners — O_EXCL, tarfs getdents dots, O_CLOEXEC

**Date:** 2026-05-14
**Source:** `merged_audit_report.md` action order items 9a (T2.1), 9b (T2.3), 14d (T2.7)
**Scope:** three independent changes, one branch + PR each, one shared spec.

## Background

These are the last unimplemented audit-driven items before the deferred
item-15 backlog. Each is small and independent. They are batched into one
spec for coherence but implemented on three separate branches off `develop`
(per CLAUDE.md: never commit to `develop`; one feature branch per change).

A fourth item from the action order (9c, PROT_WRITE-only mmap) is **not**
included — `sys_mmap` already rejects W-without-R deliberately with a
documented RISC-V rationale (kernel/syscall.c:1204-1208); treated as
closed-as-intended.

---

## Item 9a — O_EXCL (branch `feature/open-o-excl`)

### Problem

`sys_open`'s create path is guarded only on `namei` returning `-ENOENT`
plus `O_CREAT`. When `namei` succeeds (file exists) the code falls through
to `filealloc` regardless of `O_EXCL`. POSIX requires `open()` to fail with
`EEXIST` when `O_CREAT|O_EXCL` are both set and the target already exists.

### Approach

Reuse the existing candidate commit `2590cc5` ("fix(open): honor
O_CREAT|O_EXCL on existing target"), already on local branch
`feature/open-o-excl`. It is a clean 3-file diff:

- `kernel/syscall.c` — a third arm in the namei-result chain in `sys_open`
  (~line 251): when the inode resolved and `flags & O_CREAT && flags &
  O_EXCL`, `inode_put(ip)` and `return -EEXIST`. Uses the existing
  octal-with-comment style (`0100`/`0200`) consistent with surrounding code.
- `bin/o_excl_test/o_excl_test.c` — 3 cases: fresh create with `O_EXCL`
  succeeds; recreate with `O_EXCL` on existing file → `-1`/`EEXIST`;
  recreate without `O_EXCL` reopens. Uses `/tmp` (tmpfs, writable).
- `bin/init/init.c` — one line adding `/bin/o_excl_test` to the test list.

Implementation step: re-create the branch off current `develop` and
re-apply the commit (or cherry-pick), confirm it builds and the test
passes, then PR. No `proc.h` change → no `make clean` needed.

### Verification

`make`, boot, `grep -E "o_excl_test|init: [0-9]+/"` — `o_excl_test` reports
PASS and the init count goes up by one with no regressions.

---

## Item 9b — tarfs getdents `.` / `..` (branch `feature/tarfs-getdents-dots`)

### Problem

`tarfs_getdents` (kernel/fs/tarfs.c:269) iterates only `d->children` and
never emits `.` or `..`. tmpfs, sbfs, and procfs all emit them. A program
doing `getdents` on a tarfs directory (e.g. `/bin`) sees no dot entries.

### Approach

Mirror `tmpfs_op_getdents` (kernel/fs/tmpfs.c:406)'s cursor scheme. `off`
is an opaque cursor:

- cursor `0` → emit `.` with `d_ino` = this directory's own inode,
  `d_type = DT_DIR`.
- cursor `1` → emit `..` with `d_ino` = the parent inode. tarfs dir inodes
  already store `parent` (kernel/fs/tarfs.c:68). For the tarfs root, whose
  `parent` is itself/NULL, `..` resolves to the root inode (matching the
  POSIX convention that `/`'s `..` is `/`).
- cursor `2+` → `children[cursor - 2]`, as today.

The existing record-packing logic (`DIRENT64_FIXED_LEN`, 8-byte alignment,
`written + reclen > n` break, `out_next`) is reused unchanged — only the
entry source changes. Care: the `off`-th child walk at the top of the
function shifts by 2 (children now start at cursor 2).

### Verification

New `bin/tarfs_dots_test/` — call `getdents`/`readdir` on a tarfs directory
(`/bin`), assert both `.` and `..` appear exactly once with `DT_DIR`. Add
to `bin/init/init.c`. `make`, boot, grep.

---

## Item 14d — O_CLOEXEC + SYS_fcntl (branch `feature/o-cloexec`)

### Problem

The kernel ignores `O_CLOEXEC` entirely, and there is no `fcntl` syscall —
`fcntl` is a libc stub (libc/sys_stubs.c:84) that fakes `F_GETFD`/`F_SETFD`
as no-ops. Audit T2.7: "no `O_CLOEXEC`, no `fcntl(F_SETFD)`, exec doesn't
close cloexec fds."

### Design decision: where cloexec state lives

`FD_CLOEXEC` is a property of the **file descriptor**, not the open file
description — POSIX is explicit, and `dup` produces a new descriptor with
the flag cleared even though it shares the description. So the state must
live in the per-process fd table, not in the shared `struct file`.

Storage: `uint64_t cloexec_mask` in `struct pcb`, one bit per fd. `NOFILE`
is 64, so a single `uint64_t` is an exact fit. Placed near `ofile[]`.

**Build note:** adding a field to `struct pcb` (in `kernel/include/proc.h`)
requires `make clean` before `make` to verify — the Makefile has no header
dependency tracking, and a stale build produces phantom struct-corruption
panics. This is a known constraint, not a blocker.

### Kernel changes

| Site | Change |
|---|---|
| `kernel/include/proc.h` | add `uint64_t cloexec_mask;` near `ofile[NOFILE]` |
| `alloc_proc` (kernel/proc.c) | initialize `cloexec_mask = 0` |
| `proc_fork_current` (kernel/proc.c:~316) | copy `cloexec_mask` from parent to child (POSIX: fork preserves FD_CLOEXEC) |
| `sys_open` (kernel/syscall.c) | after the fd is allocated, if `flags & 02000000` (O_CLOEXEC) set `cloexec_mask |= 1ULL << fd` |
| `sys_close` (kernel/syscall.c:550) | clear the fd's bit (hygiene; bit is only meaningful while `ofile[fd]` is set) |
| `sys_dup` (kernel/syscall.c:561) | clear the new fd's bit (POSIX: dup'd fd has FD_CLOEXEC clear) |
| `sys_dup2` (kernel/syscall.c:574) | clear `newfd`'s bit; the `oldfd == newfd` early-return path leaves it untouched, which is correct |
| `do_exec` commit block (kernel/syscall.c:~1137, alongside the existing on-exec signal/alarm/altstack reset) | for each fd with its bit set: `fileclose(ofile[fd])`, `ofile[fd] = 0`; then `cloexec_mask = 0`. Placed after the point of no return (line ~1106) so a failed exec does not close fds. |
| new `sys_fcntl(int fd, int cmd, uint64_t arg)` | validate fd (`-EBADF` if out of range / not open); `F_GETFD` → return `FD_CLOEXEC` (1) if bit set else `0`; `F_SETFD` → set bit if `arg & FD_CLOEXEC` else clear; any other cmd → `-EINVAL` |
| `kernel/include/syscall.h` + dispatch switch | `#define SYS_fcntl 119`; add `case SYS_fcntl:` calling `sys_fcntl(a0, a1, a2)` |

Kernel has no `fcntl.h`; the small constants (`F_GETFD`=1, `F_SETFD`=2,
`FD_CLOEXEC`=1, `O_CLOEXEC`=`02000000`) are inlined with explanatory
comments, matching the existing `0100`/`0200` style in `sys_open`.

### libc changes

`libc/sys_stubs.c` `fcntl(int fd, int cmd, ...)`:

- `F_GETFD`, `F_SETFD` → route to the real `SYS_fcntl` syscall instead of
  faking. `F_SETFD` consumes its `int` vararg and passes it as `arg`.
- `F_DUPFD`, `F_GETFL`, `F_SETFL`, `F_GETLK`/`F_SETLK`/`F_SETLKW` → keep
  current stub behavior unchanged.
- `F_DUPFD_CLOEXEC` → currently calls `dup_to_minfd` but never sets
  cloexec (latent bug). Now fixed: `dup_to_minfd`, then on success
  `fcntl(newfd, F_SETFD, FD_CLOEXEC)`.

`open()` already forwards `flags` verbatim to the kernel — no libc `open`
change needed; `O_CLOEXEC` rides through in the existing flags argument.

### Out of scope

`F_GETFL`/`F_SETFL` real backing and file locks (`F_GETLK` etc.) keep their
current stub behavior. Only the cloexec-related `fcntl` commands get real
kernel support.

### Verification (`make clean` + boot)

Two new binaries:

- `bin/cloexec_helper/` — tiny program: `argv[1]` is an fd number; it
  attempts an operation on that fd (e.g. `fcntl(fd, F_GETFD)` or `read`)
  and exits `0` if the fd is closed (`EBADF`), `1` otherwise.
- `bin/cloexec_test/` — opens a file with `O_CLOEXEC`; asserts
  `fcntl(fd, F_GETFD) == FD_CLOEXEC`; clears via `F_SETFD` and asserts
  `F_GETFD == 0`; sets it again; then `fork` + `execv("/bin/cloexec_helper",
  {"cloexec_helper", "<fdnum>", NULL})` and `wait`s — child must exit `0`,
  proving the fd was closed across exec. Also covers a non-cloexec fd
  surviving exec as a control. Added to `bin/init/init.c`.

The helper is required because the exec-close guarantee cannot be observed
from within the calling process.

---

## Process

- Three branches off `develop`: `feature/open-o-excl`, `feature/tarfs-getdents-dots`,
  `feature/o-cloexec`. Three PRs.
- TDD per item: write the test binary, watch it fail (or, for O_EXCL,
  confirm the shipped test fails on `develop` without the fix), then implement.
- Verification harness per CLAUDE.md / HANDOFF.md: `make` (or `make clean &&
  make` for the O_CLOEXEC `proc.h` change), boot via
  `printf '...\nexit\n' | timeout 240 make -s qemu`, grep for the test name,
  `init: N/N`, `FAIL`, `panic`.
- No `Co-Authored-By` / Claude attribution in commits. Never push to `develop`.
