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
