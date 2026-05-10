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

## T1.4 — `alarm()` lying stub  *(in progress)*

**Approach chosen (planned):** Add `uint64_t alarm_tick` to PCB; new `SYS_alarm` syscall installs/cancels the deadline; `timer_handler` scans procs once per tick, sends SIGALRM via `send_signal` when expired; libc wrapper replaces the no-op stub.

**Alternatives rejected:**
- *Implement full `setitimer`/`getitimer`:* overkill for what `alarm(secs)` needs; SIGALRM-once is sufficient for typical timeout-via-alarm idioms.
- *Schedule SIGALRM as a one-shot timer interrupt:* extra hardware-timer plumbing; piggy-backing the existing 10 ms tick is simpler.

**Edge cases to handle:**
- `alarm(0)` cancels any pending alarm; returns prior remaining seconds.
- `alarm()` while another alarm is pending replaces it; returns prior remaining.
- Reset `alarm_tick = 0` on `fork` (child inherits no pending alarm — POSIX) and on `exec`.
- Round prior remaining up to whole seconds for return value.

**Out of scope:**
- `setitimer`/`getitimer` interval timers.
- Sub-second resolution alarms.
