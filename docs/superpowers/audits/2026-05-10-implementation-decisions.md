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
