# Merged Submission-Readiness Audit

**Date merged:** 2026-05-11
**Inputs:**
- `submission_audit_findings.md` — local audit, F-* IDs, severity-ranked, dynamic boot evidence (871 PASS / 0 FAIL on pre-merge develop).
- `docs/superpowers/audits/2026-05-10-submission-readiness-audit.md` — branch audit, T1/T2/T3 tiers, 84 findings. **Now in `develop`.**
- `docs/superpowers/audits/2026-05-10-implementation-decisions.md` — branch decisions log. **Now in `develop`.**

**Current state (2026-05-13):** `develop` HEAD is `ca5fc77`. PR #62 (Tier-1 batch, commit `4030461`) shipped on 2026-05-11. **Action-order items 1–8, 10–13, and most of 14 have since landed via individual PRs** (#64–#82). Remaining audit-driven work is 5 small items — see Status sync table below. A separate OPTS-driven libc backlog is now tracked at `thirdparty/open-posix/build/triage-notes.md`.

---

## Division of labor

The branch is done. **My scope** (this plan) is:

- Every finding from `submission_audit_findings.md` (F-* IDs) that the branch's commits did not already cover.
- **Tier 2 + Tier 3** from the branch audit (T2.* / T3.*).

I will **not** touch any T1.* finding — every one is fixed in `develop`.

### Status legend

| Status | Meaning |
|--------|---------|
| ✅ DONE | Landed in `develop` via PR #62 — informational only. |
| 🔴 MINE — both | Flagged by both audits, still open. High confidence. |
| 🟡 MINE — local only | Only `submission_audit_findings.md` caught it. |
| 🟠 MINE — branch only | Only the branch audit caught it (T2/T3 only). |

Findings are grouped by subsystem rather than tier.

---

## ✅ Done in develop (PR #62) — do not redo

All 17 Tier-1 items are landed. Verified by reading the audit doc + decisions log at `develop` HEAD; each item is marked `[FIXED]` in the audit.

| T-ID | Subsystem | Commit |
|---|---|---|
| T1.1 | `HEAP_MAX` cap → VMA-collision check | `aa15469` |
| T1.2 / T1.3 | `printf` full C99 spec + `%o` + `%p` + stub `%f` | `2b49931`, `4a9c5c5`, `fbef927` |
| T1.4 | `alarm()` real implementation | `8be500e` |
| T1.5 | `do_exec` reads via VFS | `7db5112`, `2567378`, `b02eadb` |
| T1.6 | `kill(-1, sig)` POSIX broadcast | `c3611fa`, `2567378` |
| T1.7 | SIGKILL force-resumes stopped | `33996b8` |
| T1.8 | NOFILE 64 / NFILE 256 | `eaa20c6`, `ca393da` |
| T1.9 | `RLIMIT_NPROC` enforced | `f85e93b` |
| T1.10 | `SIG_ERR` defined | `00c5a4b` |
| T1.11 | `recover_from_log` bounds + disk_size check | `17fb430`, `2567378` |
| T1.12 | Stack-grow heuristic dropped | `15f3737` |
| **T1.13** | **pcache × uvmcow_share refcount fix** | branch-internal (within PR #62) |
| T1.14 | `truncate`/`ftruncate` (length==0) | `4e82488`, `b02eadb` |
| **T1.15** | **`SYS_symlink` (tmpfs + sbfs creation)** | `0d7fb19`, `b02eadb` |
| T1.16 / T1.17 + partial T2.9 | init: NULL argv, backoff, orphan reap | `ce3349d` |

### Implications for my local audit findings

- **F-A05** (pcache refcount leakgit) ≡ T1.13 — **DONE**. Drop from my plan.
- **F-A01** (grandchild COW preservation) — likely covered: T1.13's `cow_pcache_refleak_test` includes a "map + grandchild-CoW (nested fork)" loop that passes 50× per cycle. **Action:** read `bin/cow_pcache_refleak_test/` after pulling the merge; if scenario (C) is the same as F-A01's reproducer, drop F-A01. Otherwise add the `fork→fork→write` case.
- Other F-* findings unaffected by the merge.

---

## 🔴 Mine — flagged by BOTH audits (highest confidence)

These items are independently in both audits *and* sit outside the branch's T1 scope, so they belong to me.

### Procfs

| ID(s) | File | What grader sees |
|---|---|---|
| **F-CC-02** ≡ **T2.6** | `kernel/fs/procfs.c:201-207` | `cat /proc/<pid>/cmdline` returns the literal string `"proc"` for every PID. **Dynamic-confirmed** in `ps` output. Fix: capture argv0 into pcb at exec; have `prod_cmdline` emit it. |
| **F-B02** ⊂ **T2.5** | `kernel/fs/procfs.c:348-370, :396` | `/proc/<pid>/cwd` missing. Add `PK_CWD`, symlink (or regular file) returning pcb's cwd. |
| **F-B07** ⊂ **T2.5** | same | `/proc/<pid>/{exe,root,fd,maps}` missing. `exe` + `root` are quick wins; `fd/` + `maps` are larger. |

### Devfs

| ID(s) | File | What grader sees |
|---|---|---|
| **F-B05 / F-B06** ≡ **T2.4** | `kernel/fs/devfs.c:248-303` | `/dev/zero`, `/dev/tty` return `-ENOENT`. Pattern off `/dev/null` for zero; alias console for tty. |

### libc — environment

| ID(s) | File | What grader sees |
|---|---|---|
| **F-E001 / F-E004** ≡ **T3.22** | `libc/stdlib.c:146-187`, `libc/exec.c:12` | `setenv/getenv/putenv/unsetenv` are no-ops; `_environ_empty[]` is the static backing. Compiled feedback L225-228 explicitly cites `putenv`. **Local audit ranks High; branch ranks Tier 3** — go with High given grader citation. |

### Memory / VMA

| ID(s) | File | Description |
|---|---|---|
| **F-A04** ≡ **T3.8** | `kernel/vma.c:52-60` | `vma_insert` panics on overlap instead of returning `-EINVAL`. |

> **F-A05 ≈ T1.13** (pcache × `uvmcow_share` refcount leak) — **DONE in PR #62.** Removed from my plan.

### Submission pipeline

| ID(s) | What | Status |
|---|---|---|
| **F-CC-01** ≡ "Strip-pipeline brittleness" | Manual strip is error-prone. | **Local has implemented `scripts/prep-submit.sh`** (whitelist-based, dynamic-verified). Branch only proposed `#ifdef SUBMIT`. Script is the stronger answer — keep using it. |

---

## 🟡 Mine — only in local audit (`submission_audit_findings.md`)

These are findings the branch audit missed. The branch isn't going to pick them up — they're entirely on me. Several tie back to **compiled grader feedback** (graded prior submissions), which is why they're high-priority.

| ID | Location | Description |
|---|---|---|
| **F-B01** | `kernel/fs/procfs.c:189-199` | `/proc/<pid>/status` missing `Pgid:` and `Sid:` lines. Compiled feedback L237-243. Also need to extend `struct proc_snap`. **Highest-priority local-only finding.** |
| **F-CC-03** | `kernel/fs/procfs.c:197` | `VmSize:\t0 kB` hardcoded. Same `proc_snap` work as F-B01. |
| **F-B03** | `kernel/fs/procfs.c:348-370` | `/proc/<pid>/statm` missing entirely. Compiled feedback L253-256. |
| **F-B04 / F-E002** | `libc/include/sys/stat.h:6-18` + `kernel/include/stat.h:4-16` | `struct stat` missing `st_blksize` and `st_blocks`. Compiled feedback L232. Touches every fs's stat filler. |
| **F-D01** | `kernel/timer.c:50`, `kernel/trap.c:70` | `timer_handler` unconditionally calls `yield()` regardless of trap source. Project doc says "no in-kernel preemption"; this violates it. Latent crash class under heavier workloads. Gate on user-mode SPP bit. |
| **F-A01** | `kernel/proc.c:351-353`, `kernel/vmem.c:193-…` | Multi-level fork (parent → child → grandchild) COW preservation. Compiled feedback L183-187. **Likely already covered by T1.13's `cow_pcache_refleak_test` scenario (C)** — verify by reading `bin/cow_pcache_refleak_test/` post-merge. If covered, drop. If not, add the missing `fork→fork→write` case. |
| **F-A02** | `kernel/vmem.c:171` | Intermediate page-table pages freed via `page_free`, not `page_put`. Latent double-free vector; not currently exercised. |
| **F-D02 / F-D03** | `kernel/syscall.c:917, :1087` | `sys_sbrk` / `sys_munmap` return bare `-1` on early-validation failure → libc translates to `EPERM` instead of `EINVAL`. Compiled feedback L2-4 cited errno discipline. |

---

## 🟠 Mine — only in branch audit (T2 / T3 — not their Tier 1)

The branch's own action plan stops at T1; they aren't picking these up. So these are mine to work on, ranked roughly by likelihood of grader test.

### Filesystem / VFS

| ID | Description |
|---|---|
| **T2.1** | `O_EXCL` not implemented — `open(path, O_CREAT\|O_EXCL)` on existing file silently succeeds. |
| **T2.2** | `O_APPEND` only seeks once at open; subsequent writes don't re-seek to `ip->size`. Need to store flags in `struct file`. |
| **T2.3** | Tarfs `getdents` skips `.` and `..`. (sbfs/tmpfs/procfs all emit them.) |
| **T2.13** + **T2.14** | Partial `munmap` of file-backed VMA returns `-EINVAL`; `vma_split` middle-cut doesn't propagate `file`/`file_off` (latent NPE). |
| **T2.27** | `sbfs_iget` *panics* on inode-cache exhaustion — userspace DoS. Return NULL → `-ENFILE`. |
| **T2.15** | No SIGBUS path for past-EOF file mmap (always SIGSEGV). |

### Memory / mmap

| ID | Description |
|---|---|
| **T2.12** | `mmap(PROT_WRITE)` without `PROT_READ` returns `-EINVAL`. Linux silently OR's in PROT_READ. One-liner. |
| **T2.29** | `vma_list_dup` partial-OOM leaks `inode_get` refs already taken. |
| **T2.30** | `flush_tlb` is always global (`sfence.vma zero, zero`). Per-VA flush is a small perf hardening. |

### Signals / process

| ID | Description |
|---|---|
| **T2.10** | `sigaction` ignores all `sa_flags`. **`SA_RESTART` is the high-impact one** (EINTR-restart loop). |
| **T2.7** | No `O_CLOEXEC`, no `fcntl(F_SETFD)`, exec doesn't close cloexec fds. |
| **T2.8** | `SIGCHLD` coalesced via 64-bit bitmap. Document "reap-in-loop" or queue siginfo. |
| **T2.9** | Orphan reap — partial fix in `ce3349d` (init-side). Other long-running processes still leak orphan zombies. |
| **T2.11** | `setuid`/`getuid` always 0. Either store in pcb or `-ENOSYS` and document. |
| **T2.28** | Pipe no `O_NONBLOCK`/`EAGAIN`; SIGPIPE raised on first byte instead of after a short-write attempt. |

### libc

| ID | Description |
|---|---|
| **T2.16** | `strtol`/`atoi` no overflow detection, no `errno=ERANGE`. Saturate at `LONG_MAX/MIN`. |
| **T2.18** | `atexit` is a no-op; `exit` doesn't run handlers or flush stdio. |
| **T2.19** | `scandir`/`alphasort` missing. Implement on opendir+readdir+qsort. |
| **T2.20** | `mntent` hardcoded table missing `/tmp` tmpfs entry. `df`/`mount` won't list `/tmp`. |
| **T2.21** | `strftime` missing `%c`, `%x`, `%X`. Common formats. |
| **T2.22** | `qsort` is O(n²) insertion sort. Replace with introsort/quicksort. |

### Shell

T2.23–T2.26 are a cluster — the branch lists every missing shell feature. Most likely grader-relevant:
- **T2.23** subset: single-quote handling, `;` separator, `2>` stderr redirect, builtins `true`/`false`/`test`.
- **T2.25** Shell exec failure should write to stderr and exit 127/126, not exit 1 to stdout.
- **T2.24** Shell raw `read(0)` — no line editor; backspace breaks. (Bigger lift.)
- **T2.26** PATH not searched — only `/bin/` tried. (Depends on env mutators landing first.)

---

## Reconciliation notes

1. **Severity disagreement worth honoring:**
   - **F-E001 / T3.22** (env mutators). Local: High (cites compiled feedback L225-228, specifically `putenv`). Branch: T3 cosmetic. **Treat as High** — keep at item #3 in the action order.

2. **Items the local audit ruled out:**
   - F-E003 (sigsuspend missing) — **wrong**, it's at `libc/bb_compat.c:129`. Drop.
   - F-A03 (heap zero-fill in multi-level forks) — `zerofill_test` PASSES. Drop.

3. **Toolchain note (from branch audit):**
   Build is `-march=rv64imac_zicsr_zifencei -mabi=lp64` — no F/D extensions, soft-float ABI. Any FP-using test cannot link, so stub `atof/strtod`, `%f`, etc. returning 0 are honest, not bugs. **Don't waste time "fixing" them.**

4. **Strip pipeline:** local has `scripts/prep-submit.sh` (whitelist, dynamic-verified). Keep using it; ignore the `#ifdef SUBMIT` recommendation in the branch audit.

5. **Truncate is partial:** T1.14 fix only handles `length == 0` and `length == ip->size`. If the grader tests `ftruncate(fd, N>0)` to extend or partial-shrink, it'll get `-EINVAL`. Out of scope for now but worth knowing.

---

## My action order (post-merge)

**Status sync — 2026-05-13.** Verified against `develop` HEAD `ca5fc77`. Most items shipped via individual PRs since the doc was first written. Remaining items are flagged ❌; everything else is closed.

| # | Item | Status | PR / Commit |
|---|---|---|---|
| 1 | F-B01 + F-CC-03 + F-CC-02 (procfs Pgid/Sid/VmSize/comm) | ✅ DONE | PR #67 `c07641e` |
| 2 | F-B04 / F-E002 (`st_blksize`, `st_blocks`) | ✅ DONE | PR #60 `9109096` |
| 3 | F-E001 / F-E004 (env mutators) | ✅ DONE | PR #69 `aab869f` |
| 4 | F-B02/B03/B07 (procfs cwd/statm/exe/root) | ✅ DONE | PR #68 `ffc0f06` |
| 5 | F-B05/B06 (`/dev/zero`, `/dev/tty`) | ✅ DONE | PR #66 `d52d518` |
| 6 | F-D01 (timer gate on user-mode SPP) | ✅ DONE | PR #65 `4a84ec7` |
| 7 | T2.10 SA_RESTART | ✅ DONE | PR #70 `4ddaff9` |
| 8 | T2.27 sbfs_iget exhaustion → ENFILE | ✅ DONE | PR #64 `3121808` |
| 9a | T2.1 O_EXCL | ❌ OPEN — header defined, kernel doesn't honor | local: `feature/open-o-excl` (commit `2590cc5`, unmerged) |
| 9b | T2.3 tarfs `./..` lookup | ❌ OPEN | — |
| 9c | T2.12 PROT_WRITE-only mmap | ❌ OPEN | — |
| 10 | F-A01 multi-level CoW regression | ✅ likely DONE | covered by `cow_pcache_refleak_test` (verify by reading) |
| 11 | F-A04 vma_insert returns -EINVAL | ✅ DONE | PR #71 `8687156` |
| 12 | F-A02 page-table via page_put | ⏳ PR pending | local: `feature/pgtable-page-put` |
| 13 | F-D02/D03 sbrk/munmap -EINVAL | ✅ DONE | PR #72 `30a04ff` |
| 14a | T2.23/T2.25 shell hardening | ✅ DONE | PR #82 `e850f1a` |
| 14b | T2.2 O_APPEND | ✅ DONE | PR #80 `dec8905` |
| 14c | T2.13/T2.14 partial munmap | ⏳ PR pending | local: `feature/partial-munmap` |
| 14d | T2.7 O_CLOEXEC | ❌ OPEN — header defined, kernel doesn't honor | — |
| 14e | T2.16 strtol/strtoul overflow | ✅ DONE | PR #75 `ca5fc77` |
| 14f | T2.18 atexit | ✅ DONE | PR #76 `71e553e` |
| 14g | T2.19 scandir | ✅ DONE | PR #78 `997b542` |
| 14h | T2.20 mntent `/tmp` | ✅ DONE | PR #79 `bd16a2d` |
| 14i | T2.21 strftime %c | ✅ DONE | PR #77 `e79101d` |
| 14j | T2.22 qsort | ✅ DONE | `libc/stdlib.c` has `qsort` impl |
| 15 | T3 cosmetics, `flush_tlb` global, etc. | ⏸ DEFER | — |

### What's still actionable from this audit

Three small items still need work; two more are in PR pipeline:

- **T2.1 O_EXCL** (item 9a) — `feature/open-o-excl` already has a candidate patch unmerged. Verify and PR.
- **T2.3 tarfs `./..` lookup** (item 9b) — one-liner in `kernel/fs/tarfs.c`.
- **T2.12 PROT_WRITE-only mmap** (item 9c) — one-liner in `kernel/vma.c` or syscall validation.
- **T2.7 O_CLOEXEC** (item 14d) — kernel side: honor on `exec` by closing FDs with the flag set.

In pipeline (local branches awaiting push/PR):

- **F-A02 page-table via page_put** (item 12) — `feature/pgtable-page-put`
- **T2.13/T2.14 partial munmap** (item 14c) — `feature/partial-munmap`
- **shebang / vmstk** (not in original audit) — `feat/shebang-and-vmstk`

### New backlog source: OPTS gap-list (post-2026-05-13)

A separate body of work is now driven by the Open POSIX Test Suite gap-discovery tooling at `thirdparty/open-posix/`. See `thirdparty/open-posix/build/triage-notes.md` for the current libc-gap backlog (sigaltstack, clock_settime, sigpending, killpg, sig{wait,hold,ignore,set,relse}, seteuid). These are surfaced by running OPTS against current libc and are independent of the audit-doc items above; both backlogs can be worked in parallel.

---

## Suggested first-commit shape (item #1)

The first three items in the action order all touch `proc_snap` + procfs formatters. Combine them:

1. Extend `struct proc_snap` (`kernel/fs/procfs.c:183`) with `pgid`, `sid`, `comm[16]`, `vm_size_kb`.
2. Populate from the pcb wherever `proc_snap` is filled.
3. Update `prod_status` to emit `Pgid:`, `Sid:`, and a real `VmSize:` after `PPid:`.
4. Update `prod_cmdline` to emit `comm` from snap (with fallback `"proc"` only if snap is empty).
5. Capture `argv0` into pcb's `comm` field at `do_exec` post-image-load.

Single commit, single PR, addresses F-B01 + F-CC-02 + F-CC-03 in one go. Verify with `cat /proc/$$/status` and `cat /proc/$$/cmdline` from sh.
