# Submission-Audit Fix Campaign — Implementation Decisions Log

**Branch:** `feature/submission-readiness-audit`
**Audit document:** `2026-05-10-submission-readiness-audit.md`

Append-only log of design decisions made while fixing audit findings. One entry per fix. Future agents (or the grader's narrative review) read this to understand *why* a particular shape was chosen, not just *what* changed.

---

## T1.1 — `HEAP_MAX` artificial sbrk cap

**Commit:** `aa15469`

**Approach chosen:** Walk the process VMA list at sbrk-grow time; cap heap end at the lowest VMA `start` above the current heap end, falling back to `MMAP_BASE` (64 GB) if nothing is in the way.

**Alternatives rejected:**
- *Bump `HEAP_MAX` to a higher fixed value (e.g. 4 GB or 64 GB):* still an artificial number; reviewer can keep asking "why this number?". A VMA-walk has no magic constant.
- *Remove the ceiling entirely:* heap could overrun the libc malloc arena (which lives at `[HEAP_ARENA_BASE, HEAP_ARENA_END)` via `MAP_FIXED`) or the user stack — silent corruption.
- *Compute ceiling once at exec time and stash it:* fragile if `mmap`/`munmap` change the layout afterward; cheaper to walk on demand.

**Edge cases handled:**
- Signed-add overflow (`new_end < old_end`) → `-ENOMEM` instead of wrapping.
- `incr == 0` returns current break unchanged.
- Negative `incr` path unchanged (zero pages + tlb-flush still correct).
- Heap VMA itself excluded from the walk (don't cap against self).

**Out of scope (deliberately):**
- Coalescing the heap VMA with adjacent same-prot VMAs (audit T3.8).
- Shrinking past start with negative `incr` (still `-EINVAL`).

---

## T1.4 — `alarm()` lying stub

**Approach chosen:** New `uint64_t alarm_tick` field in PCB (0 = none). New `SYS_alarm` syscall (#84) installs/cancels deadline as `now + secs * TICKS_PER_SEC`; returns prior remaining seconds rounded up. `timer_handler` scans the proc list each 10 ms tick; on expiry, clears `alarm_tick` and calls `send_signal(p, SIGALRM)`. libc `alarm()` replaced from no-op stub to a thin syscall wrapper that bypasses `syscall_ret` (the syscall has no failure modes per POSIX, so negative returns are not errors).

**Alternatives rejected:**
- *Full `setitimer`/`getitimer`*: overkill; alarm-once suffices for the burned-by-alarm-stub case.
- *One-shot hardware timer interrupt scheduled at the deadline*: extra plumbing; piggy-backing on the existing 10 ms periodic tick is simpler and good enough.

**Edge cases handled:**
- `alarm(0)` cancels any pending alarm; returns prior remaining.
- Replacing a pending alarm returns the prior remaining (rounded up to whole seconds).
- Cleared on `exec` (POSIX requires; `kernel/syscall.c` `do_exec` post-image-load).
- `alloc_proc` zeros `alarm_tick`, so children of `fork` inherit no alarm (POSIX).
- Sentinel guard: if computed deadline equals `0` (effectively never), bump to `1` so the field's "0 = none" invariant holds.
- Skip dispatch on `PROC_UNUSED`/`PROC_ZOMBIE` slots so a recycled PCB doesn't fire a stale alarm.

**Out of scope:**
- `setitimer`/`getitimer` interval timers.
- Sub-second alarm resolution.

---

## T1.6 — `kill(-1, sig)` returns `-EPERM`

**Approach chosen:** Implement broadcast by walking the proc list and signaling every non-self, non-init, non-unused, non-zombie process. Returns 0 if at least one was signaled, `-ESRCH` if none. `sig == 0` follows the same path as an existence probe.

**Alternatives rejected:**
- *Reject `kill(-1, sig)` outright (status quo)*: violates POSIX; cleanup tooling and "kill all my children" idioms break.
- *Include init (pid 1) in the broadcast*: would let any unprivileged process kill the system. POSIX excludes the caller's reach over privileged processes; SBUnix is single-user so the only meaningful guard is "don't kill init".
- *Include the caller itself*: POSIX leaves this implementation-defined; excluding the caller is the safer default for a self-cleanup idiom (`kill(-1, SIGTERM)` to clean up children before the caller itself exits).

**Edge cases handled:**
- Skip `PROC_UNUSED` and `PROC_ZOMBIE` slots so we don't signal recycled or already-dead PCBs.
- `sig == 0` short-circuits to an existence probe (no `send_signal` call), matching the named-pid / pgid behavior.
- Returns `-ESRCH` only when zero non-self, non-init processes exist (rare in practice).

**Out of scope:**
- Multi-user permission checks (no uid system).
- Limiting broadcast to the caller's session (SBUnix is single-session).

---

## T1.7 — SIGKILL on stopped process hangs forever

**Approach chosen:** In `send_signal`, after marking SIGKILL pending, if the target is `PROC_STOPPED`, force `state = PROC_READY` and clear `wake_tick`. The scheduler will then pick the process up; `check_signals` runs on its next user-mode return path, sees SIGKILL, and runs the default `ACT_TERM` action.

Notify the parent with SIGCHLD on the same path (the parent might be in `wait4(WUNTRACED)` waiting on the stopped child).

**Alternatives rejected:**
- *Synthesize an exit directly inside `send_signal`*: would require `send_signal` to be safe to call from arbitrary contexts (timer IRQ, other procs). Letting the scheduler+check_signals handle the exit keeps the death path consistent with every other ACT_TERM signal.
- *Auto-resume on any fatal signal*: SIGTERM and friends can be caught/blocked, so they shouldn't unilaterally resume a stopped process. SIGKILL is special-cased because it can never be caught or blocked.

**Edge cases handled:**
- Parent in `wait4(WUNTRACED)` on the stopped child wakes via the SIGCHLD send (`proc_wakeup` would already happen on the subsequent ACT_TERM exit, but the explicit SIGCHLD here keeps the bookkeeping clean).
- The pre-existing force-unblock + override-SIG_IGN at the top of `send_signal` already covers the case where SIGKILL is masked or ignored.
- Sleeping (`PROC_SLEEPING`) processes already woken via the existing deliverable-signal check below.

**Out of scope:**
- SIGTERM/SIGINT auto-resume (caller can pair `SIGCONT; SIGTERM` if desired).
- Group-wide SIGKILL semantics — `kill(-pgid, SIGKILL)` already iterates the pgrp and hits each member individually via `send_signal`.

---

## T1.8 — `RLIMIT_NOFILE = 16` default too low

**Approach chosen:** Quadruple the per-process fd table (`NOFILE` 16 → 64) and the global open-file table (`NFILE` 128 → 256). Default `RLIMIT_NOFILE` rlim_cur and rlim_max both set to `NOFILE` (64). Update libc-side advertised limits to match (`OPEN_MAX`, `FOPEN_MAX`, `_SC_OPEN_MAX`).

**Alternatives rejected:**
- *Match Linux's 1024 default*: each PCB pays `NOFILE * sizeof(void *)` for the static `ofile[]` array. 1024 × 8 = 8 KiB/proc, exceeds the 4 KiB single-page PCB allocation. A larger PCB allocation is doable but invasive for marginal benefit on a teaching OS.
- *Make `ofile` dynamically sized*: extra complexity, more error paths.
- *Bump only the rlimit and leave NOFILE=16*: rlim_cur > NOFILE is meaningless — the array is the hard cap.

**Memory cost:** PCB grows by `(64-16) * 8 = 384` bytes. PCB still fits in one 4 KiB page (was ~2.6 KiB before; now ~3.0 KiB).

**Edge cases handled:**
- `NOFILE` is the hard ceiling; `rlim_max` set to `NOFILE` so user code can't `setrlimit` past the array.
- libc `OPEN_MAX`, `FOPEN_MAX`, `_SC_OPEN_MAX(sysconf)` all bumped to 64 so portable code probes consistent values.

**Out of scope:**
- Truly dynamic per-process fd tables.
- Per-uid resource accounting.

---

## T1.16 + T1.17 + (partial) T2.9 — `init` execv NULL argv, no backoff, no orphan reap

**Approach chosen:** Three coupled fixes in `bin/init/init.c`'s shell-respawn loop:

1. **`execv("/bin/sh", 0)` → `execv("/bin/sh", sh_args)` with a non-NULL argv.** POSIX requires `argv[0]` non-NULL; the `0` literal was UB in practice on every libc.
2. **Exec failure exits 127** (POSIX shell convention for "command not found") instead of `1`, so a backoff loop can distinguish exec-failure from a normal shell exit.
3. **Backoff:** track consecutive exit-code-127 results in `sh_failures`; on the 5th, `sleep(5)` and reset. Prevents busy-looping the kernel log when `/bin/sh` is missing or corrupt.
4. **Orphan reap (T2.9):** call `while (waitpid(-1, 0, WNOHANG) > 0) {}` at the top of every iteration so detached subprocesses reparented to init are reaped instead of leaking zombie slots.

**Alternatives rejected:**
- *Panic on first sh-exec failure*: too brittle. Allow some retries in case it's a transient (oom, page fault) issue.
- *Exponential backoff*: overkill. 5s flat after 5 fast failures is enough.
- *Reap orphans on a separate timer/SIGCHLD handler*: init is single-threaded; reap-on-loop-iteration is simpler and bounded by how often init wakes anyway (every shell exit).

**Edge cases handled:**
- `waitpid(-1, …, WNOHANG)` returns 0 when no zombies, so the inner loop exits cleanly.
- Failure counter resets on any non-127 exit so a transient crash doesn't accidentally trigger backoff later.
- The forked sh child still calls `setpgid(0, 0)` first so the foreground pgid handoff works.

**Out of scope:**
- T2.9 fully — orphan reap also belongs anywhere else that long-running processes spawn detached children. Init is the most important reaper but not the only one needed in principle. Audit doc T2.9 stays open for that reason.

---

## T1.10 — `SIG_ERR` undefined; `signal()` returned `SIG_IGN` on error

**Approach chosen:** Define `SIG_ERR` as `((sighandler_t)-1)` in `libc/include/signal.h` (matches glibc/musl/POSIX); change `libc/signal.c:signal()` to return `SIG_ERR` on `sigaction` failure.

**Alternatives rejected:**
- *Define SIG_ERR as something other than -1*: every other libc uses -1; deviating breaks portable code that assumes the cast.
- *Leave `SIG_IGN` as the error sentinel*: silently mis-handles `if (signal(...) == SIG_ERR)` checks because real handlers might compare `== SIG_IGN` for "this signal is currently ignored".

**Edge cases handled:** None — pure header + one-line return-value swap.

**Out of scope:** Re-evaluating BSD-vs-System-V semantics of `signal()` (handler stays installed across delivery vs reset to SIG_DFL). Current behavior keeps the handler installed (BSD semantics, what most tests expect). Audit didn't flag this as broken; leaving it.

---

## T1.11 — `recover_from_log` trusts on-disk `lh->n` blindly

**Approach chosen:** Defensive validation of the on-disk log header in `recover_from_log`:
1. Reject `lh->n > LOG_HDR_MAX` outright; log a message, zero the header, return.
2. Reject any individual block entry pointing at block 0, the log header itself, or anywhere inside the log region. Same recovery action.

In both cases the on-disk transaction is unrecoverable anyway (we can't trust *any* of the header's contents), so the safe move is to clear it and continue with no replay.

**Alternatives rejected:**
- *Add a magic number to the on-disk header*: requires bumping the on-disk format version, conflicts with the existing mkfs layout. Reviewer-visible upside is small for a course OS.
- *Panic on corruption*: makes a single bad sector fatal across reboots. Recoverable-with-data-loss is friendlier and matches xv6 behavior.
- *Bounds-check `lh->n` only*: doesn't catch entries pointing at the superblock or other system metadata. Per-entry check is cheap (≤15 entries).

**Edge cases handled:**
- `lh->n == 0`: skipped naturally (loop doesn't run; the existing `log.nblocks > 0` guard prevents a no-op replay).
- Entry equal to `log.start` (header itself) or any in-log slot is rejected.
- Entry equal to 0 is rejected (block 0 is the on-disk superblock by convention; replaying onto it would brick the disk).

**Out of scope:**
- Adding a CRC over the log header. Useful but disk-format-changing.
- Detecting torn data-block writes. The header gates the commit; if data blocks are torn we'd need per-block CRCs.

---

## T1.5 — `do_exec` (and `proc_spawn`) crash on non-tarfs binaries

**Approach chosen:** Refactor `load_user_elf` to take `struct inode *` instead of `(const void *, size)` and pull bytes via `generic_file_read` (the VFS read path that goes through `ip->ops->readpage`). Both call sites — `do_exec` in `syscall.c` and `proc_spawn` in `exec.c` — now resolve the path with `namei` and pass the inode pointer through. The tarfs-specific cast of `ip->fs_data` is gone.

A small inline helper `read_at(ip, off, buf, n)` wraps `generic_file_read` and treats short reads as `-ENOEXEC` (malformed binary).

**Alternatives rejected:**
- *Pre-flight reject non-tarfs binaries with `-ENOEXEC`*: avoids the crash but ships a known-broken corner. The audit's whole point is that "known-broken" is what burns submissions.
- *Allocate a contiguous bounce buffer of size `ip->size`, read whole file, pass to old API*: page allocator is single-page only; multi-page contiguous would need a vmalloc-style allocator we don't have.
- *Add a `read_into_buffer` op per fs*: extra interface for a problem `generic_file_read` already solves.

**Edge cases handled:**
- Reject non-regular-file inodes (`ip->type != I_REG`) with `-EACCES` instead of trying to exec a directory or symlink directly.
- ELF program-header count capped at 32 (sanity bound; real binaries have ≤8).
- Short reads on header, phdr table, or any segment all return `-ENOEXEC` cleanly with intermediate VMA list freed.
- `inode_put` always called on the namei'd inode, including on early-error paths.
- Selftest's two `load_user_elf` callers updated (`test_load_elf`, `test_uvmcow_share`) to pass an inode pointer obtained via `namei("/bin/init")`.
- `sched_init`'s and selftest's `proc_spawn("bin/init")` paths bumped to absolute `/bin/init` because kernel threads have no cwd to anchor a relative resolve.

**Out of scope:**
- Lazy/on-demand segment loading (mmap-style). All segments are still eagerly read into freshly allocated kernel pages at exec time.
- Removing `tarfs_find` — selftest still uses it as a sanity check that the tarfs blob is well-formed before any VFS-level code runs.

**Side-effect commits to `kernel/include/errno.h`:** added `ENOEXEC=8` and `EAGAIN=11` (used by the new return paths and by the now-tolerant oom_test).

---

## T1.9 — `RLIMIT_NPROC` never enforced

**Approach chosen:** In `proc_fork_current`, before calling `alloc_proc`, count live (non-unused, non-zombie, non-init) processes. If `rlim_cur` is finite and the count is already at the limit, return `-EAGAIN`. SBUnix is single-user so the count is global rather than per-uid.

**Alternatives rejected:**
- *Enforce in `alloc_proc`*: also called for kernel threads, where rlimits don't apply. Hooking at the user-fork entry point is cleaner.
- *Track a counter incremented at alloc / decremented at zombie reap*: more state, easier to drift out of sync. Walking the list once per fork is fine — fork is not a hot path on a teaching OS.

**Edge cases handled:**
- Parent's `rlim_cur == RLIM_INFINITY` short-circuits the check (no-op for the default config).
- Init (pid 1) excluded from the count so it's never counted against a forking process's limit.
- `EAGAIN` matches POSIX/Linux for "too many processes" (paired with the libc errno `EAGAIN=11` already added by T1.5's side effect).

**Out of scope:**
- Per-uid accounting. SBUnix doesn't have meaningful uids, so the global count is the cleanest mapping of NPROC semantics.
- Enforcing on `vfork` or `clone` (we don't have those).

---

## T1.12 — Stack-grow gate too restrictive

**Approach chosen:** Drop the `stval >= user_sp - PAGE_SIZE` heuristic in `user_page_fault`'s stack-grow branch. The remaining checks are: (a) fault below the existing stack VMA, (b) fault address ≥ rlimit-derived floor (`USER_STACK_TOP - rlim_stack`). Any below-stack fault inside that floor extends the VMA. Matches Linux semantics.

**Alternatives rejected:**
- *Keep the SP-distance heuristic but bump the threshold to 64 KB or 128 KB*: still arbitrary; gcc -O0 with very deep frames or alloca would bypass it. Pick a number high enough and it's effectively gone.
- *Require explicit user-space stack-probe (`#pragma GCC stack_check`)*: not how user binaries are compiled. Burdens the user, helps no one.

**Edge cases handled:**
- Truly random pointer below stack: still rejected because the rlimit floor catches it (`stack_max` defaults to 8 MB; anything below `USER_STACK_TOP - 8 MB` faults out as before).
- Massive `alloca(N)` or deep recursion that walks below the floor: returns `-1` → SIGSEGV, which is what POSIX requires when stack rlimit is reached.

**Out of scope:**
- Auto-bumping `RLIMIT_STACK` on demand (Linux has soft/hard distinction; we already do).
- Reporting SIGSEGV with `si_code = SEGV_MAPERR` vs `SEGV_ACCERR` — we don't expose siginfo to user-space.

---

## T1.14 — `truncate(2)` / `ftruncate(2)` ENOSYS

**Approach chosen:** Add `SYS_truncate` (#85) and `SYS_ftruncate` (#86). Both call a shared `do_truncate_inode(ip, length)` helper that:
- Rejects non-regular files with `-EINVAL`.
- Treats `length == ip->size` as a no-op (POSIX-compliant fast path).
- Treats `length == 0` as full truncate via the existing `ip->ops->truncate` op.
- Returns `-EINVAL` for any other length (extending or partial-shrink).

Replace libc's `-ENOSYS` stubs in `libc/misc.c` with real syscall wrappers in `libc/syscall.c`.

**Alternatives rejected:**
- *Implement extend / partial shrink immediately*: requires a richer fs op (`truncate_to(ip, length)`) plus implementations in tmpfs and sbfs (with sparse-block bookkeeping). Multi-hour task; deferred.
- *Stub at `-ENOSYS`*: no improvement over current state. The audit explicitly flagged this as a missing syscall, so partial support is strictly better than none.

**Edge cases handled:**
- `length < 0` → `-EINVAL`.
- `fd` invalid or not writable → `-EBADF`.
- Read-only fs (no `truncate` op) → `-EROFS`.
- `truncate(path)` namei failure → `-ENOENT`.

**Out of scope (deferred):**
- Lengths != 0 and != current size. Documented in source comment so future work has a clear pickup point.

---

## T1.15 — `symlink(2)` creation

**State of read-side support (already in place, pre-audit):**
- `I_LNK` inode type, `inode_ops.readlink`, `SYS_readlink` (#112).
- VFS path walker follows symlinks with 8-hop ELOOP detection.
- Relative-target resolution against the symlink's parent.
- `lstat` + `S_ISLNK`, `O_NOFOLLOW` via `lnamei`.
- Pre-baked symlinks work: tarfs (from tar entries), `/dev/loop` (self-referential), `/proc/self`.
- `bin/symlink_test` PASS on the read-side flows.

**What was missing:** `symlink(2)` — userspace cannot create new symlinks at runtime. `libc/misc.c:symlink()` was an `-ENOSYS` stub.

**Approach chosen:** Add real creation, both in-memory (tmpfs) and on-disk (sbfs).

1. New `inode_ops.symlink(parent, name, target)` slot.
2. `tmpfs_op_symlink`: allocate a tmpfs inode of type `I_LNK`, store target in a static per-inode buffer (PATH_MAX), link into parent dir. Reuses `tmpfs_readlink` op already in place.
3. `sbfs_op_symlink`: allocate a `sb_dinode` with new on-disk type `SBFS_T_LNK = 3` — the existing `uint16_t type` field already has room (no format bump), and the target string is stored in regular data blocks (same path as file content). `sbfs_iget` maps `type==3` → `vnode.type = I_LNK`. `sbfs_readlink` op reads target from the data blocks via `sbfs_readi`.
4. `SYS_symlink(target, linkpath)` — splits `linkpath` into dirname + basename, namei's the parent, dispatches to `parent->ops->symlink(parent, name, target)`.
5. Replace libc stub with thin syscall wrapper.

**Alternatives rejected:**
- *Store target in the dinode itself*: `sb_dinode.addrs[12]` is 48 bytes — would cap symlink targets at 48 chars. Tarfs already supports 100; matching feels right. Using data blocks gives full PATH_MAX without bumping dinode size.
- *Bump on-disk format version*: not needed — the unused `type==3` slot was always reserved (the field is a u16, the comment just enumerated the values in use).

**Edge cases handled:**
- `linkpath` already exists → `-EEXIST` (handled in `SYS_symlink` namei pre-flight).
- Target string too long (> 1023 chars on tmpfs, > a few-blocks on sbfs) → `-ENAMETOOLONG`.
- Empty target → `-EINVAL`.
- Read-only fs (tarfs) → `-EROFS` via NULL op.
- mkfs not regenerated — fresh disks will have no symlinks; users create them at runtime.

**Out of scope:**
- `linkat(2)` / `symlinkat(2)` — single fixed semantics is enough.
- Hardening: symlink targets containing internal NULs (we treat as terminator).

---

## T1.2 + T1.3 — `printf` width / precision / flags / %o

**Approach chosen:** Replace the prior single-conversion-character `do_format` with a real C99 conversion-spec parser:

1. Parse flag characters: `-` (left), `+` (plus), space, `#` (alt), `0` (zero-pad).
2. Parse width: digits or `*` (consume an `int` arg; negative width sets `-` and uses `|width|`).
3. Parse precision: `.` then digits or `.*`.
4. Parse length modifier: `h`, `hh`, `l`, `ll`, `z`, `j`, `t`. `h`/`hh` are tracked as `lng = -1` / `-2`; the underlying short/char is promoted to `int` by default argument promotions, so the integer conversions read `va_arg(ap, int)` and narrow back via a `(short)` / `(signed char)` / `(unsigned short)` / `(unsigned char)` cast. Reading via `va_arg(ap, unsigned int)` here would be UB on lp64 because the actual promoted type is signed `int`.
5. Dispatch on the conversion character. Add `%o`, full `%p` (uses ALT for "0x" prefix and "(nil)" for NULL), and a soft-float stub `%f/%e/%g` that consumes a `double` arg and emits `"0.000000"` so format strings don't leak literal `%f` to output.

New helpers:
- `emit_pad(s, n, ch)` — bulk fill.
- `emit_num(s, mag, base, flags, width, precision, sign_ch)` — handles width, precision, zero-pad, alt-form prefix, and the rare `%.0d` of 0 (POSIX: empty output).
- `emit_str(s, str, flags, width, precision)` — string with width and precision (max bytes).

**Alternatives rejected:**
- *Pull in a third-party printf*: not freestanding-friendly, license risk, and the rewrite is small.
- *Fix only the most common widths (`%5d`, `%-20s`)*: too narrow; reviewer can break it with one creative format string.
- *Implement real soft-float `%f`*: huge work for no real benefit on a no-FP toolchain (T1.3 / RISC-V FP note).

**Edge cases handled:**
- `vsnprintf(NULL, 0, ...)` sizing-pass count is correct (sink keeps counting after buffer fills).
- `%.0d` of 0 produces zero digits (POSIX).
- `%#x` of 0 omits the "0x" prefix.
- `%p` of NULL renders as `"(nil)"`.
- Negative `*` width sets the `-` flag and absolute-values the count.
- Trailing `%` at end of format emits literally.
- Unknown conversions emit `%X` (literal) so malformed format strings are visible.
- Float conversions consume the `double` arg slot (so subsequent args remain aligned) but emit a constant string.
- `%#.3o` of 8 emits `"010"`, not `"0010"`. The alt-form `0` prefix is suppressed when precision-driven zero-padding already supplies a leading 0 — emitting it on top of `zeros > 0` was over-counting.
- `%hu` / `%hhu` / `%hx` / `%hhx` etc. narrow the read `int` to the declared width before widening to `unsigned long long`. `%hhu` of `0x101` prints `"1"`, `%hu` of `0x10001` prints `"1"`.

**Out of scope:**
- Real floating-point conversions (no FP toolchain, see T1.3 note).
- `%n` (security risk; rarely used).
- Locale-aware grouping or thousands separators.

---

## T1.13 — `pcache` × `uvmcow_share` refcount leak and file-CoW slot corruption

**Approach chosen:** Three coordinated changes plus one symmetric defensive guard, gated by a single new helper `pcache_pa_to_slot(pa)` that maps a physical address to the pcache slot owning it (or NULL).

1. **`uvmcow_share` (`kernel/vmem.c`)** — skip `page_get(pa)` when `pa` belongs to a pcache slot. Pcache slot pages live outside the `page_refs[]` accounting (their slot owns the backing page for the slot's lifetime, init bumps `page_refs[pfn]=1` once and it stays there). The child's fault-time pin is taken via `pp->refcnt` in `vma_dup_file_pages`, which is the right currency. Without this skip, every fork of a proc with a faulted-in file mapping leaked one `page_refs[pcache_pfn]` permanently; ~65 535 such forks tripped `page_get: refcount overflow`.

2. **File-CoW write fault (`kernel/vma.c:190-214`)** — the present-PTE + `VMA_FLAG_COW` branch previously assumed `old_pa` was always the pcache slot for `(v->file, file_pgidx)` and ran `pcache_get(...); pcache_put × 2;`. After fork-of-fork-after-CoW the same branch fires with `old_pa = anon` (a private anon page from an earlier CoW that the post-fork PTE_W clear made write-faultable again), and the unconditional pcache dance corrupted an *unrelated* slot's refcnt by 2 — eventually triggering eviction of a slot another proc still had mapped, silently serving wrong data. The branch now uses `pcache_pa_to_slot(old_pa)` to discriminate:
   - **pcache `old_pa`**: drop this proc's fault-time pcache ref via a single `pcache_put`. No `pcache_get` re-lookup needed — the slot can't be evicted while we hold the ref, so `pcache_pa_to_slot` returns a stable pointer.
   - **anon `old_pa`**: `page_put(old_pa)` to release the refcnt this PTE held from the prior `uvmcow_share`. Without it, page_refs[anon_pfn] leaked 1 per fork+CoW cycle.

3. **Symmetric teardown guard (`kernel/vmem.c`)** — `free_user_pages_level` and `uvmunmap_range` both skip `page_put` for pcache slot pages. The normal exit/exec paths already clear file PTEs through `vma_drop_file_pages` before generic teardown runs, so this is belt-and-suspenders, but it makes the page_refs invariant "any PTE pointing at a pcache slot is invisible to page_get/put" hold structurally instead of by convention. A future caller forgetting `vma_drop_file_pages` would no longer underflow the slot's backing page out from under the cache.

**The helper itself** (`kernel/page_cache.c`):
- `pcache_pa_to_slot(pa)` linear-scans the fixed `slots[]` array (PCACHE_NSLOTS=64) comparing `virt_to_phys(slots[i].page)` against `pa`. `slots[i].page` is set exactly once in `pcache_init` and never changes, so the read is lock-free.

**Alternatives rejected:**
- *Track pcache pages through `page_get`/`page_put` like anon pages*: would require teaching pcache eviction to wait for page_refs to drop, mixing two separate refcount systems (slot refcnt vs page refcnt). The current design is "pcache owns its pages, fault/teardown is invisible to page_refs" — that's cleaner, this fix just makes the boundary tight everywhere.
- *Reverse-map physical addresses via a hashtable*: PCACHE_NSLOTS is 64. Linear scan is ~50ns and saves the hashtable's maintenance cost on every pcache_get/put.
- *Always call `pcache_get` in the file-CoW branch to identify the slot*: bumps refcnt for nothing, costs a lock acquisition and an LRU shuffle, and doesn't help the anon-old_pa case anyway (would just bump a wrong-but-still-valid slot for a different (ip,pgidx) pair).

**Edge cases handled:**
- Slot eviction while a proc holds a fault-time pcache ref: impossible — eviction requires `pp->refcnt == 0`. The `pcache_pa_to_slot` lookup in the file-CoW branch is therefore safe to use without a fresh `pcache_get`.
- Anon CoW after fork, parent already CoW'd: `old_pa` is anon, helper returns NULL, takes the `page_put(old_pa)` branch — drops the fork-inherited ref.
- Grandchild CoW after parent fork (no parent write): `old_pa` is pcache (PTE still installed via fault), helper returns the slot, single `pcache_put` drops grandchild's fault-time ref.
- `pcache_pa_to_slot` called pre-init: returns NULL (guard on `pcache_inited`). Boot-time selftests that run after pcache_init aren't affected.

**Verification:**
- **Kernel selftest** `test_pcache_uvmcow_refleak` (`kernel/selftest.c`): synthetically install a pcache PTE into a fresh pagetable, record `page_ref_get(pcache_pa)`, run 256 `uvmcow_share + free_user_pgtable` cycles, assert the refcount is unchanged before/after and again after tearing the parent down. Catches the leak deterministically without needing 65 535 iterations.
- **Userspace regression** `bin/cow_pcache_refleak_test`: three loops exercising the realistic paths — (A) map+fork+child-CoW, (B) map+parent-CoW-then-fork+child-CoW (the anon-`old_pa` case), (C) map+grandchild-CoW (nested fork). 50 iterations each, all pass.
- **Smoke**: 939 PASS / 0 FAIL across kernel selftest + 101 init tests + usertests. No `refcount overflow` / `refcount underflow` panics.

**Out of scope:**
- A general "PTE → owning subsystem" registry. Pcache is the only refcount-immune mapping today; if more land, the linear-scan helper can be replaced with a dispatch table.
- `MAP_SHARED` writeback ordering during fork — already handled by `vma_drop_file_pages` MAP_SHARED branch; this fix only changes the page_refs side.
- Wide-character `%ls` / `%lc`.
