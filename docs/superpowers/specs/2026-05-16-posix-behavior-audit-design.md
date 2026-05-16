# POSIX Behavior-Stub & Exec-Path Audit — Design Spec

**Date:** 2026-05-16
**Branch:** `feat/posix-surface-fixes`
**Author session context:** Triggered by grader feedback on `access(2)` returning
unconditional failure. `b1bfcde` fixed that item plus four siblings
(`system`, `pathconf`, `uid/gid`, `fcntl F_GETFL/F_SETFL`). This spec
audits the broader class — every libc/kernel surface where the wrapper
exists but never reaches its kernel implementation, or reaches it and
lies.

---

## 1. Goal

Eliminate one class of grader feedback: "Your wrapper exists, but it
returns failure / silent success / never touches the kernel, so a
portable program using it misbehaves." Deliver an audit document
enumerating every instance of this class plus fixes for the seven items
most likely to be probed.

## 2. Background

### 2.1 Grader signal

Grader probed `access("/bin/<interp>", X_OK)` to test whether an
interpreter binary was reachable before invoking it. Our `access` was a
libc stub returning -1/ENOSYS. Probe answered "missing" for every path,
including paths that `open()` would succeed on. Downstream interpreter
test bailed without ever trying `execv`. Same class of bug exists across
the libc surface.

### 2.2 Existing audit (prototype-shape only)

`docs/posix-audit-2026-05.md` audits **header prototype shapes** (38
test files in `tests/posix/`, all pass). It catches `read` declared as
`long read(int, void*, long)` vs POSIX `ssize_t read(int, void*,
size_t)`. It does **not** catch wrappers that compile cleanly but
return ENOSYS or silent-success at runtime. That is this spec's
contribution.

### 2.3 Branch baseline (`b1bfcde`)

Already shipped on this branch since `develop` HEAD:

| Item | Class | Mechanism |
|---|---|---|
| `access(2)` | hard-failing stub | new `SYS_access=114`, namei + X_OK mode-bit check |
| `system(3)` | partial | fork + execv("/bin/sh", "-c", cmd) + waitpid |
| `pathconf`/`fpathconf` | hardcoded EINVAL | switch over `_PC_*` constants |
| uid/gid identity | always-0 | pcb gains uid/gid fields, real syscalls |
| `fcntl F_GETFL/F_SETFL` | partial | kernel reconstructs from file flags |
| `sigmask_defer_test`, `sigmask_exec_test` | regression probes | tests covering separate grader feedback on signal masking |

This spec extends `b1bfcde`'s posture. Audit doc opens with this table
so graders see full state, not delta.

## 3. Scope

**Locked:** Approach A — grader-visible behavior. Critical = anything
where wrong libc behavior makes a portable user program silently
misbehave. Seven items in Section 6. Everything else (Approach C
residue) lands in the audit doc's future-scope appendix.

**Out of scope:** signal subsystem (covered by `b1bfcde` sigmask
probes), terminal/job control (no grader signal), `/proc` field
completeness, mount semantics, shell builtins.

## 4. Architecture

Three artifacts:

1. **`docs/superpowers/audits/2026-05-16-posix-behavior-audit.md`** —
   full enumeration. Critical + deferred sections. Each item: symbol,
   current behavior, expected POSIX behavior, severity, fix sketch,
   test path, fix commit SHA (or "deferred").
2. **`bin/<sym>_test/<sym>_test.c`** — one regression test per
   critical item. Registered in `bin/init/init.c::tests[]`. Fails on
   `develop` HEAD before the fix commit; passes after.
3. **Source fixes** in `libc/` and `kernel/`. One logical commit per
   critical item.

Pattern matches existing branch convention (`access_test`,
`posix_surface_test`, `sigmask_defer_test`, etc. — all
`bin/<name>_test/<name>_test.c`, all registered in init).

## 5. Methodology

### 5.1 Discovery

- `grep -nE "errno\s*=\s*ENOSYS|return\s+-1.*ENOSYS"` across `libc/`.
- For each header in `libc/include/`, list declared symbols and check
  for definitions in `libc/*.c`. Mismatches are undef-symbol candidates
  (caught `select`/`pselect`).
- Cross-check libc wrappers against `SYS_*` table in
  `libc/include/sys/syscall.h`. Wrapper exists but no SYS_ entry → stub.
- Read `kernel/syscall.c` dispatch + `kernel/exec.c` for partial-impl
  surfaces (envp drop, stat-via-open).
- Reference POSIX-2024 (SUSv5) where shape disputes arise; existing
  `tests/posix/` already encodes shape expectations.

### 5.2 Per-item verification protocol

1. Write `bin/<sym>_test/<sym>_test.c` exercising real POSIX semantics
   (not compile-only).
2. Build, boot QEMU, confirm test FAILS on the pre-fix tree.
3. Land the fix commit.
4. Confirm test PASSES.
5. Audit doc entry records fix SHA.

### 5.3 Audit doc entry format

```
### <symbol>
- Current: <one-line description of broken behavior>
- POSIX: <one-line expected behavior>
- Severity: build-breaker | silent-corruption | grader-visible | latent
- Test: bin/<sym>_test/<sym>_test.c
- Fix: <commit SHA> (or "deferred — future scope")
```

Future-scope entries use the same shape with `Fix: deferred`.

## 6. Critical fix list (7 items, Approach A)

Ordered by independence. Each row is one self-contained commit.

| # | Symbol | Class | Kernel work | libc work | New SYS_ |
|---|---|---|---|---|---|
| 1 | `select`/`pselect` | undef-symbol (link-breaker) | none | new `libc/select.c` — poll-over-known-fds + sleep loop; `tv` timeout honored | none |
| 2 | `pread`/`pwrite` | missing-impl | thin dispatch in `kernel/syscall.c` (read/write at offset without perturbing f_pos) | thin ecall wrappers | `SYS_pread=120`, `SYS_pwrite=121` |
| 3 | `execve` envp | partial-impl (silent data loss) | `kernel/exec.c` extends user stack frame builder to include envp pointers; new dispatch case in `kernel/syscall.c` | `execve` routes to new SYS; `execv` unchanged | `SYS_execve=122` |
| 4 | `utimes`/`utime`/`utimensat` | missing-impl | new dispatch; writes inode `mtime`/`atime`. tarfs returns EROFS (decision locked, see §10); sbfs/tmpfs/procfs honor. | wrappers in `libc/time.c` | `SYS_utimensat=123` |
| 5 | `openat` (dirfd ≠ AT_FDCWD) | hard-failing stub | new dispatch — resolve relative path against the dirfd's path | route `openat` to new syscall | `SYS_openat=124` |
| 6 | `stat()` without open | partial-impl | new dispatch — `namei` + populate `struct stat` directly (mirror existing `SYS_lstat`, but follow symlinks) | replace `libc/sys_stubs.c::stat` body with direct ecall | `SYS_stat=125` |
| 7 | `mkfifo` | hard-failing stub | none (named-pipe subsystem out of scope) | none | none |

Item 7 is a **documentation + regression-guard test**, not a fix:
`mkfifo` stays ENOSYS, audit doc records *why* (no FIFO subsystem),
test asserts `mkfifo()==-1 && errno==ENOSYS` so a silent flip to
silent-success can't slip in unnoticed.

### 6.1 Item-1 design note: libc-side `select`

Tradeoff: cheaper than `SYS_select` but not interruptible by signal as
cleanly as a real one. Acceptable because grader tests probe one or
two fds with a short timeout. Loop checks readiness via existing fd
metadata (console-readable iff input pending; pipe-readable iff
nonempty; pipe-writable iff not full) and `usleep(1ms)` between
sweeps. tv = NULL blocks until ready; tv = {0,0} polls once.

### 6.2 Item-3 design note: `execve` envp

`kernel/exec.c::do_exec` currently builds the user stack as `[argc] [argv ptrs] [argv strings]`. Extend to `[argc] [argv ptrs] [envp ptrs] [argv strings] [envp strings]`. AT_NULL terminators between sections, matching SysV LP64. Existing `SYS_execv=24` keeps the empty-envp path unchanged (back-compat for in-tree users that don't pass envp); new `SYS_execve=122` takes envp arg. `libc/exec.c::execve` routes to the new syscall, `execv` unchanged.

### 6.3 Item-6 design note: `stat` fallback

If new `SYS_stat` returns ENOSYS (defensive: shouldn't happen
post-fix), keep an open()-fstat()-close() fallback in libc so existing
callers don't regress mid-deploy. The fallback is also useful if a
future FS rejects namei for some path class.

## 7. Future-scope appendix (Approach C residue)

Listed in the audit doc with `Fix: deferred — future scope`. Not in
this round's work.

**Silent-success stubs** (legitimate for permissionless FS but
grader-probe-able): `chmod`, `fchmod`, `chown`, `fchown`, `lchown`,
`umask` (libc-only), `fsync`, `fdatasync`, `sync`, `sethostname`.

**Hard ENOSYS staying ENOSYS** (no subsystem backing them): `mknod`,
`F_GETLK`/`F_SETLK`/`F_SETLKW`, `poll`/`ppoll`.

**Missing-entirely**: `fchdir`, `getlogin`/`getlogin_r`, `confstr`,
`nice`, `lockf`, `getentropy`/`getrandom`, `linkat`, `unlinkat`,
`fchownat`, `renameat`, `symlinkat`, `readlinkat`, `mkdirat`,
`fstatat`, `mknodat` (the *at family becomes feasible once `openat`
lands in §6).

**Partial implementations**: `execvp` PATH search ignores `$PATH`
env; `getgroups`/`getgrouplist` hardcoded to `{0}`; `uname`
release/version cosmetic; `gethostname` libc-static.

## 8. Deliverables

### 8.1 Files created

```
docs/superpowers/audits/2026-05-16-posix-behavior-audit.md
bin/select_test/select_test.c
bin/pread_pwrite_test/pread_pwrite_test.c
bin/execve_env_test/execve_env_test.c
bin/execve_env_test/env_child.c
bin/utimes_test/utimes_test.c
bin/openat_test/openat_test.c
bin/stat_direct_test/stat_direct_test.c
bin/mkfifo_test/mkfifo_test.c
libc/select.c
```

### 8.2 Files modified

```
libc/include/sys/syscall.h         # +6 SYS_ entries (120-125)
kernel/include/syscall.h           # mirror entries
kernel/syscall.c                   # +5 dispatch cases
kernel/exec.c                      # envp stack-frame extension
libc/syscall.c                     # +5 thin ecall wrappers
libc/sys_stubs.c                   # stat() body replaced; openat() body replaced
libc/exec.c                        # execve routes to SYS_execve
libc/time.c                        # +utimes/utime/utimensat wrappers
bin/init/init.c                    # register 7 new tests
Makefile                           # add 7 new bin/<name>_test dirs to USER_BINS (or equivalent)
```

### 8.3 Commit sequence

Seven commits, one per critical item, each self-contained. Eighth
commit lands the audit doc itself, referencing all prior SHAs.

```
1.  feat(posix): select/pselect libc-side implementation
2.  feat(posix): pread/pwrite real syscalls
3.  feat(posix): execve propagates envp to child
4.  feat(posix): utimes/utime/utimensat real syscalls (tarfs EROFS)
5.  feat(posix): openat with arbitrary dirfd
6.  feat(posix): stat() bypasses open() via SYS_stat
7.  test(posix): mkfifo regression guard (stays ENOSYS)
8.  docs(posix): behavior-stub audit doc — 7 critical shipped, future scope listed
```

## 9. Verification

Per commit: `make qemu` boot, `init` test loop reports `N/N tests
passed` including the new test for the just-landed item. `make
posix-check` (existing prototype harness) still reports 38 passed.

End-to-end: all 8 commits land, branch boots, `init` reports
`(prior_N + 8)/N tests passed`, audit doc references each of seven
SHAs.

## 10. Decisions log

| # | Question | Decision | Rationale |
|---|---|---|---|
| Q1 | `utimensat` on tarfs: EROFS strict or chmod-style silent-success? | **EROFS strict** | sbfs/tmpfs *can* honor timestamps. Silent-success on tarfs while sbfs honors = grader sees inconsistency. Don't extend chmod's compromise. |
| Q2 | Audit doc location: `docs/posix-behavior-audit-2026-05.md` or `docs/superpowers/audits/`? | **`docs/superpowers/audits/2026-05-16-posix-behavior-audit.md`** | Matches existing `2026-05-10-submission-readiness-audit.md` convention. |
| Q3 | `mkfifo` — implement or stay ENOSYS? | **Stay ENOSYS, document, regression-guard test** | Named-pipe subsystem is multi-week work, out of submission window. Documenting + guarding is honest. |
| Q4 | `select` kernel or libc? | **libc** | Cheap; grader tests are simple single-fd cases. `SYS_select` is future work. |

## 11. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | libc `select` is racy under multi-fd or signal-interrupted patterns | Doc flags it; defer real `SYS_select` to next round |
| R2 | `SYS_stat` direct path may regress procfs/devfs callers | Keep open()-fallback in libc if SYS_stat returns ENOSYS; verify `bin/stat_test` still passes |
| R3 | execve envp stack-frame change can brick `init` | TDD discipline; existing `init::tests[]` is the canary; one-commit revert path |
| R4 | utimensat tarfs EROFS may surprise callers that expect chmod-style success | Audit doc + test explicitly document the choice; existing chmod stays as-is |
| R5 | 8 boot cycles to verify per commit | Acceptable; flagged so plan accounts for time |
| R6 | Audit not exhaustive — some classes (kernel silent-success without ENOSYS) won't surface in greps | Doc explicitly scopes itself as "best-effort, not exhaustive"; next cycle expands |
