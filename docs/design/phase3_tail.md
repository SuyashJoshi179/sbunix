# Phase 3 Tail — Close Out the User-Space Substrate

## 1. Goal

Turn the fork/exec/wait substrate into something the rest of the plan
can build on without footnotes. Specifically: timer actually preempts
user loops, bad user dereferences kill only the faulting process,
the trivial process-control syscalls (`getppid`, `yield`, `sleep`)
exist, and every fork/exec/wait cycle frees exactly what it
allocated — no pagetable leaks.

No new subsystems. This is the last chance to make the existing one
correct before we pile file descriptors, VFS, and a shell on top.

## 2. Preconditions

From Phase 3 core (already merged into `develop`):

- `kernel/proc.c` with round-robin scheduler, `proc_fork_current`,
  `proc_exit_current`, `proc_wait_current`, `proc_sleep`,
  `proc_wakeup`, `free_proc`.
- `kernel/syscall.c` with `SYS_exit`, `SYS_write`, `SYS_exec`,
  `SYS_fork`, `SYS_wait`, `SYS_getpid`.
- `kernel/trap.S` + `kernel/trap.c` trap path that handles S-mode
  ecall (syscall), timer interrupts, and panics on everything else.
- `kernel/timer.c` where `timer_handler()` already calls `yield()`
  unconditionally.
- `libc/syscall.c` with wrappers for `write`, `getpid`, `fork`,
  `wait`; `libc/include/unistd.h`.
- User binaries under `bin/<name>/` auto-built into the tarfs:
  `init`, `echo`, `fork_test`, `pid_test`, `multi_fork_test`,
  `addrspace_test`, `write_test`.

## 3. What's actually broken or missing

A survey of `develop` at branch point:

1. **`kernel/trap.S:117` clobbers `t0` on every trap return.** After
   restoring all GPRs from the frame, the exit sequence does
   `csrr t0, sstatus; andi t0, t0, 0x100; bnez t0, 4f` to decide
   whether to swap sp↔sscratch. That overwrites `x5` (`t0`), which was
   just restored from the frame one instruction earlier.
   Latent today because existing user binaries don't carry live `t0`
   across ecalls. The moment we enable real timer preemption of user
   code mid-basic-block, it becomes a corrupting bug.

2. **`kernel/trap.c` panics on every non-syscall exception.** Lines
   42–43 are `printk(...); while(1){}`. A user NULL-deref kills the
   kernel.

3. **`kernel/proc.c:71 free_proc` never frees `p->pagetable`.** So
   every forked/spawned user process leaks its entire user page-table
   tree on reap: the L2 root + any L1/L0 page-table pages + the data
   pages themselves. The orphan-reap path in `scheduler_run` (line
   315) has the same bug — it calls `free_proc` only.

4. **`kernel/proc.c:315` frees the zombie while its SATP may still
   be loaded.** `scheduler_run` does `swtch` back from the dying
   process and then inspects `current->state == ZOMBIE`. At that
   point the hart's SATP is still whatever the last user process was
   using (the loop's `write_satp(kernel_pgtable)` hasn't run yet for
   this iteration — it runs at the *top* of the next loop). Freeing
   page-table pages while the MMU is translating through them is a
   "works-until-it-doesn't" bug. Must switch to kernel SATP before
   freeing.

5. **No `SYS_getppid`, `SYS_yield`, `SYS_sleep`** — roadmap-listed
   trivial syscalls. libc has no wrappers either.

6. **No test binary exercises preemption.** `timer_handler` already
   calls `yield()`, so preemption is technically live, but nothing
   demonstrates it. We want a test that would *visibly fail* if
   preempt were removed.

7. **No test binary exercises a bad user dereference.** Currently
   impossible because the kernel panics instead of killing.

8. **No leak test.** The roadmap demands a fork/exec/wait × 1000
   invariant check on the page free-count.

This phase fixes items 1–4 and adds 5–8.

## 4. Concepts & data structures

### 4.1 `struct pcb` additions

New field in `kernel/include/proc.h`:

```c
struct pcb {
    ...
    uint64_t wake_tick;   // if state == PROC_SLEEPING and wake_tick > 0,
                          // the tick count at which this proc becomes READY
    ...
};
```

`wake_tick == 0` means "blocked in wait(), wake on proc_wakeup()". Any
non-zero value is a deadline owned by the timer handler. Keeps the
two kinds of sleep (wait-for-event and sleep-for-ms) in one state.

### 4.2 Tick counter exposed

`kernel/timer.c`'s `static uint64_t ticks` becomes non-static: either
export `uint64_t timer_ticks(void)` from `kernel/include/timer.h`, or
move the counter to a header-visible extern. Prefer the accessor.

### 4.3 Fault classification

`kernel/trap.c` needs a small helper:

```c
// Returns 1 if scause is a user-survivable exception we should kill
// the process on.  0 for kernel bugs (panic).
static int is_user_fault(uint64_t cause_code) {
    switch (cause_code) {
        case 0:   // instruction address misaligned
        case 2:   // illegal instruction
        case 4:   // load address misaligned
        case 5:   // load access fault
        case 6:   // store/AMO address misaligned
        case 7:   // store/AMO access fault
        case 12:  // instruction page fault
        case 13:  // load page fault
        case 15:  // store/AMO page fault
            return 1;
        default:
            return 0;
    }
}
```

The SPP bit from the saved sstatus (trapframe[TF_SSTATUS] & SPP)
decides whether to kill or panic.

## 5. File-by-file changes

### 5.1 `kernel/trap.S` — fix the return-path t0 clobber

Rewrite the exit sequence to branch on SPP *before* restoring GPRs.
Two exit paths, one for S-mode return, one for U-mode return, each
does the full GPR restore plus its own sp handling. Macroize the GPR
restore block if you care about duplication; the plain version is
also fine — trap.S is meant to be obvious, not clever.

Sketch:

```asm
    # (after trap_handler returns)
    ld t0, 248(sp)
    csrw sepc, t0
    ld t0, 256(sp)        # saved sstatus
    csrw sstatus, t0

    # Decide return mode from the value in t0 (saved sstatus),
    # before we clobber t0 by restoring GPRs.
    andi t0, t0, 0x100    # SPP
    beqz t0, .Lret_u

.Lret_s:
    # S-mode return: sp is already on the kernel stack, no swap.
    ld x1,   0(sp)
    ld x3,  16(sp)
    ...
    ld x31, 240(sp)
    # Note: we do NOT restore x2/sp here — we're running on it.
    addi sp, sp, 288
    sret

.Lret_u:
    # U-mode return: after GPR restore, swap sp with sscratch
    # so the hart comes out of sret on the user stack.
    ld x1,   0(sp)
    ld x3,  16(sp)
    ...
    ld x31, 240(sp)
    addi sp, sp, 288
    csrrw sp, sscratch, sp
    sret
```

Property the fix must maintain: the value in `sscratch` on return to
U must be the kernel stack top (what trap.S swapped *out* of sp on
entry). Since the entry path puts kernel sp there via `csrrw sp,
sscratch, sp`, the exit `csrrw sp, sscratch, sp` puts user sp back
into sscratch, and the next trap's entry swap restores kernel sp
again. Confirm by eyeballing: entry and exit each do one swap.

### 5.2 `kernel/trap.c` — fault classification and kill path

Replace the `while (1) {}` panic with:

```c
void trap_handler(uint64_t scause, uint64_t sepc, uint64_t stval,
                  uint64_t *trapframe) {
    uint64_t is_interrupt = scause & (1UL << 63);
    uint64_t cause_code   = scause & 0xFF;

    if (is_interrupt) {
        switch (cause_code) {
            case 5: timer_handler(); return;
            default:
                panic("unknown interrupt: cause=%lu sepc=%lx",
                      cause_code, sepc);
        }
    }

    // Exception
    if (cause_code == 8) {  // U-mode ecall
        trapframe[TF_SEPC] += 4;
        int64_t ret = syscall_dispatch(trapframe[TF_A7], trapframe);
        trapframe[TF_A0] = (uint64_t)ret;
        return;
    }

    // From U-mode: kill the process and let the scheduler move on.
    // From S-mode: kernel bug; panic.
    int from_user = (trapframe[TF_SSTATUS] & SSTATUS_SPP) == 0;
    if (from_user && is_user_fault(cause_code)) {
        printk("[fault] pid=%d killed: scause=%lx sepc=%lx stval=%lx\n",
               current_proc() ? current_proc()->pid : -1,
               scause, sepc, stval);
        proc_exit_current(-14);   // -SIGSEGV equivalent status
        // proc_exit_current swtches away; never returns
    }

    panic("kernel exception: scause=%lx sepc=%lx stval=%lx",
          scause, sepc, stval);
}
```

`panic()` is an existing helper (referenced in proc.c). If not yet
fatal-loud, make it so: print, then `while (1) wfi;`.

**Important:** `proc_exit_current` swtches away, so `trap_handler` for
the dying process never returns to `trap_vector` — the half-restored
trap frame is simply abandoned on the kstack page that will be freed
when the process is reaped. That's fine as long as the frees happen
with the kernel pgtable loaded (see §5.4).

### 5.3 `kernel/proc.c` — free pagetable; reap on kernel SATP

**`free_proc` (proc.c:71):** add one call at the end:

```c
void free_proc(struct pcb *victim) {
    // ... unlink ...
    if (victim->pagetable) {
        free_user_pgtable(victim->pagetable);
        victim->pagetable = 0;
    }
    if (victim->kstack_page) {
        page_free(victim->kstack_page);
        victim->kstack_page = 0;
    }
    page_free(victim);
}
```

`free_user_pgtable` only walks the user half (L2 0–255) and frees
leaf data pages + intermediate page-table pages + the root page
itself. It does NOT touch kernel mappings in the upper half. Safe to
call on the current process's (now-dead) pagetable as long as the
hart is translating through a *different* pagetable at the moment of
call — so the caller must switch SATP first.

**`scheduler_run` orphan-reap path (proc.c:315):** switch SATP before
the free:

```c
if (current && current->state == PROC_ZOMBIE && current->parent_pid == 0) {
    struct pcb *z = current;
    current = 0;
    write_satp(make_satp(kernel_pgtable));
    flush_tlb();
    free_proc(z);
}
```

**`proc_wait_current`:** the wait path calls `free_proc(p)` on the
zombie child from the parent's kernel stack. The hart is translating
through the *parent's* pagetable, not the child's, so
`free_user_pgtable(child->pagetable)` is safe. No change needed
here beyond the `free_proc` change already in 5.3.

### 5.4 New syscalls

#### 5.4.1 `kernel/include/syscall.h`

Add numbers. Must match `libc/syscall.c`. Numbering is append-only
from now on:

```c
#define SYS_getppid 11
#define SYS_yield   12
#define SYS_sleep   13   // arg: ms
```

#### 5.4.2 `kernel/syscall.c`

```c
static int64_t sys_getppid(void) {
    struct pcb *p = current_proc();
    return p ? (int64_t)p->parent_pid : -1;
}

static int64_t sys_yield(void) {
    yield();
    return 0;
}

static int64_t sys_sleep(uint64_t ms) {
    if (ms == 0) { yield(); return 0; }
    proc_sleep_ms(ms);
    return 0;
}
```

Dispatch additions trivial.

#### 5.4.3 `kernel/proc.c` — `proc_sleep_ms`

New helper that uses `wake_tick`:

```c
void proc_sleep_ms(uint64_t ms) {
    struct pcb *p = current;
    if (!p) return;
    uint64_t now = timer_ticks();
    uint64_t wake = now + ms_to_ticks(ms);
    if (wake <= now) wake = now + 1;  // never-zero deadline
    p->wake_tick = wake;
    proc_sleep(p);     // sets state = SLEEPING, swtches away
    p->wake_tick = 0;  // clear on wake so waiters don't misuse it
}
```

`ms_to_ticks(ms)` approximates `ms * ticks_per_sec / 1000`. Since
`TIMER_INTERVAL = 10_000_000` in `kernel/timer.c`, tick rate is
`CPU_TIMEBASE / 10_000_000`. On QEMU's `virt` machine the timebase
is 10 MHz → 1 tick/sec. That's too coarse for `sleep(50)`. Two
options:

- **a.** Lower `TIMER_INTERVAL` to 100_000 (100 ticks/sec). Raises
  tick load; still trivial.
- **b.** Parameterize. Add `TICKS_PER_SEC` constant in `timer.h`.

Recommendation: **option a + b**. Set `TIMER_INTERVAL` so one tick ≈
10 ms (100 Hz), define `TICKS_PER_SEC = 100`, and `ms_to_ticks(ms) =
(ms + 9) / 10`. Document the derivation in `timer.c`.

#### 5.4.4 `kernel/timer.c` — wake sleepers

`timer_handler` now has a second job: walk the proc list, and for
every `PROC_SLEEPING` proc with `wake_tick != 0 && wake_tick <=
ticks`, set `state = PROC_READY` and `wake_tick = 0`.

```c
void timer_handler(void) {
    ticks++;
    sbi_set_timer(read_time() + TIMER_INTERVAL);

    // Wake any timed sleepers.
    for (struct pcb *p = proc_list_head(); p; p = p->next) {
        if (p->state == PROC_SLEEPING && p->wake_tick != 0 &&
            p->wake_tick <= ticks) {
            p->state = PROC_READY;
            p->wake_tick = 0;
        }
    }

    yield();
}
```

Walking the proc list from an interrupt handler is fine on a single
hart because:

- proc-list mutations (`alloc_proc`, `free_proc`) run from regular
  kernel code with interrupts implicitly enabled, but we can flip
  them off for those two functions if we aren't already — check
  `free_proc` in particular.
- `yield()` immediately after iteration means we don't hold onto
  stale pointers across a scheduling event.

Add `proc_list_head()` to `kernel/include/proc.h` as a tiny accessor
that returns `procs` (the static list head in proc.c).

#### 5.4.5 `libc/syscall.c` + `libc/include/unistd.h`

Add:

```c
int   getppid(void);
int   sched_yield(void);   // POSIX name; unistd uses sched_yield
int   usleep(unsigned long us);  // libc convenience
int   sleep_ms(unsigned long ms);
```

Implementation mirrors existing wrappers. `usleep` rounds up to
milliseconds; fine for our resolution.

Decision: the *kernel* syscall takes ms, not us — resolution is 10
ms anyway. The libc `usleep` multiplies/divides as needed and is a
convenience for callers that think in us.

### 5.5 New user binaries

Each is a folder under `bin/<name>/` with a single `.c`. The
Makefile picks them up automatically (see Makefile rule for
`build/rootfs/bin/%`).

#### 5.5.1 `bin/preempt_test/preempt_test.c`

```c
#include <stdio.h>
#include <unistd.h>

int main(void) {
    int pid = fork();
    const char *tag = (pid == 0) ? "child" : "parent";
    for (int i = 0; i < 10; i++) {
        // Tight loop with no syscall between iterations, to prove
        // that *preemption* (not cooperation) interleaves the two.
        for (volatile int j = 0; j < 200000; j++) { }
        printf("%s tick %d\n", tag, i);
    }
    if (pid > 0) { int st; wait(&st); }
    return 0;
}
```

Passes if the output interleaves parent and child ticks roughly
evenly instead of parent finishing all 10 before child starts (which
is what pre-preemption behavior would look like with this control
flow, once we remove the unconditional yield inside `timer_handler`
for... wait, we keep the yield. The test passes as long as we don't
regress).

Stronger version: each process writes to a shared-via-stdout log of
"parent=N child=M" where numbers strictly increase, and a monitoring
rule asserts interleaving. For Phase 3 tail, eyeballing the output
in CI is sufficient.

#### 5.5.2 `bin/segv_test/segv_test.c`

```c
#include <stdio.h>
#include <unistd.h>

int main(void) {
    int pid = fork();
    if (pid == 0) {
        printf("child: about to deref NULL\n");
        *(volatile int *)0 = 0xdead;   // store to unmapped page
        printf("child: NOT REACHED\n");
        return 99;
    }
    int st = 0;
    int r = wait(&st);
    printf("parent: waited, child pid=%d status=%d\n", r, st);
    return 0;
}
```

Passes if parent prints the "waited" line and the kernel does not
panic. Exit status should be the `-14` from `proc_exit_current`.

#### 5.5.3 `bin/sleep_test/sleep_test.c`

```c
#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("tick\n"); sleep_ms(100);
    printf("tick\n"); sleep_ms(100);
    printf("tick\n"); sleep_ms(100);
    return 0;
}
```

Primarily a smoke test for the syscall. Visual assertion: output
pacing matches expectations.

#### 5.5.4 `bin/yield_test/yield_test.c`

```c
#include <stdio.h>
#include <unistd.h>

int main(void) {
    int pid = fork();
    const char *tag = (pid == 0) ? "c" : "p";
    for (int i = 0; i < 20; i++) {
        printf("%s%d ", tag, i);
        sched_yield();
    }
    printf("\n");
    if (pid > 0) { int st; wait(&st); }
    return 0;
}
```

Passes if the output is roughly `p0 c0 p1 c1 ...`, proving
cooperative handoff via `yield`.

### 5.6 Kernel selftests

Add `selftest_leak()` to `kernel/selftest.c`. Called from
`selftest_run()` after the existing tests. Outline:

```c
static unsigned long pmem_free_count(void) {
    // Walk the freelist and count.  Add a helper to pmem.c:
    //   unsigned long pmem_free_count(void);
    ...
}

static void test_leak_fork_exec_wait(void) {
    printk("[SELFTEST] -- leak: spawn/free 1000x --\n");
    unsigned long before = pmem_free_count();

    for (int i = 0; i < 1000; i++) {
        struct pcb *p = proc_spawn("bin/init");
        st_check(p != 0, "spawn init");
        // Fake a clean exit: mark zombie and free.  In the full test,
        // we'd run the process through the scheduler once — that's
        // hard from selftest_run (we haven't started the scheduler
        // yet).  For the pre-scheduler selftest, just tear down the
        // spawn path manually, exercising the free logic.
        free_user_pgtable(p->pagetable);
        p->pagetable = 0;
        free_proc(p);
    }

    unsigned long after = pmem_free_count();
    st_check(before == after, "pmem free count invariant 1000x");
    if (before != after)
        printk("[SELFTEST]   before=%lu after=%lu delta=%ld\n",
               before, after, (long)after - (long)before);
}
```

The pre-scheduler version tests the allocator/free symmetry only. A
second version, run after the scheduler is up, actually fork/exec/waits
and is harder to do as a kernel selftest. For Phase 3 tail, ship the
pre-scheduler version and add a `// TODO: runtime leak test` comment.

The alternative — a user-space leak test run from the scheduler —
requires a `SYS_selftest` hook (Phase 9 work). Defer.

Add `unsigned long pmem_free_count(void)` to `kernel/pmem.c` / `.h`
— walks the freelist once.

### 5.7 `kernel/timer.c` — interval adjustment

```c
#define TICKS_PER_SEC  100
// QEMU virt machine: mtime frequency = 10 MHz.
#define TIMER_INTERVAL (10000000UL / TICKS_PER_SEC)  // = 100_000

static inline uint64_t ms_to_ticks(uint64_t ms) {
    return (ms * TICKS_PER_SEC + 999) / 1000;   // round up
}
```

## 6. Syscall ABI & error codes

| # | Name | args | returns |
|---|---|---|---|
| 11 | `getppid` | — | parent pid (≥0) or -1 |
| 12 | `yield` | — | 0 |
| 13 | `sleep` | `uint64_t ms` | 0 |

No errno additions. `sleep` with very large values is fine — it just
blocks until the deadline (or, worst case, until tick counter
overflow, which at 100 Hz is ~5.8 billion years).

The roadmap's "negative errno at syscall boundary" rule is preserved:
no new failure cases were introduced.

## 7. Test plan (additions for this phase)

| Test | Where | What it proves |
|---|---|---|
| `preempt_test` | tarfs user bin | timer interleaves two CPU-bound user loops |
| `segv_test` | tarfs user bin | bad deref → process dies, kernel survives, other procs continue |
| `yield_test` | tarfs user bin | `sched_yield` hands off cooperatively |
| `sleep_test` | tarfs user bin | `sleep_ms` actually pauses and returns |
| `test_leak_fork_exec_wait` | kernel selftest | 1000× spawn/free symmetric in pmem |
| existing `test_uvmcopy` | unchanged | pagetable deep-copy still works |

Wire the new user binaries into `sched_init` (proc.c:327) so they
run at startup, matching the existing convention.

## 8. Gotchas

Beyond the cross-cutting list in the roadmap:

- **`yield()` inside `timer_handler` runs with SIE=0.** Hardware
  cleared it on trap entry. `yield()` itself disables/restores SIE
  around `swtch`; that's compatible but means the scheduler's `wfi`
  path in `scheduler_run` has to re-enable SIE (it does — see
  `scheduler_run` line 286). Don't break that.
- **`proc_exit_current` must not leave `current` stale.** It sets
  `state = PROC_ZOMBIE` and swtches. The scheduler's orphan-reap
  path (and the new "switch SATP first" fix) reads `current->state`
  on return. If any future change sets `current = NULL` in
  `proc_exit_current`, the reap check breaks.
- **Walking `procs` from `timer_handler`** — interrupts are disabled
  in S-mode handlers, but a kernel path could be modifying the list
  at the same time on... no we're single-hart. IRQs off inside the
  trap handler means no concurrent mutation. Good. If you later add
  SMP, this loop is one of the first places to take a real lock.
- **`proc_sleep_ms(0)`** — semantics: equivalent to `yield()`. Don't
  set `wake_tick=0` and then `proc_sleep` or the proc sleeps forever.
- **`wake_tick` monotonicity.** Uses `ticks`. If `ticks` ever becomes
  non-monotonic (reset on reboot inside the same kernel lifetime is
  fine, but a future "virtualized time" feature might not be),
  revisit.
- **Killing the process that's currently running** in `trap_handler`:
  the trap frame is on that process's kernel stack. `proc_exit_current`
  marks ZOMBIE and swtches to scheduler; the scheduler doesn't read
  the half-restored frame. When the zombie is reaped, both the
  kstack page and the (now stale) trap frame on it are freed in one
  shot. No leak.
- **The `panic()` helper must be loud and non-returning.** Current
  usage in `proc.c` expects a printf-style first arg — confirm when
  implementing. If `panic` doesn't exist yet as a macro/function,
  add it to `printk.h` / `printk.c`:
  ```c
  __attribute__((noreturn))
  void panic(const char *fmt, ...);
  ```

## 9. Open questions / decisions punted to later

- **Preempting kernel threads** is currently unconditional because
  `timer_handler` calls `yield()` regardless. Acceptable — we have
  no long kernel critical sections that are safe to preempt in
  user-mode-only fashion. Revisit if we ever grow one.
- **`sched_yield` vs `yield` naming.** POSIX spells it `sched_yield`
  in `<sched.h>`; the kernel syscall doesn't care. Keep the libc
  wrapper POSIX-compatible.
- **`usleep` vs `sleep`.** We're exposing `sleep_ms` and `usleep`.
  POSIX `sleep(seconds)` is trivial to add on top; do it later when
  a user demands it.
- **`wait4`/exit-status encoding.** The kernel currently passes raw
  `int`. POSIX encodes signal vs exit in a single word. Keep raw
  for now; widen when signals land in Phase 8.
- **Runtime (post-scheduler) leak test.** Belongs to Phase 9 via
  `SYS_selftest`. This phase ships the pre-scheduler symmetric test
  only.

## 10. Exit criteria

The phase is done when **all** of the following hold:

- [ ] `make qemu` boots, `selftest_run` passes including the new
      `test_leak_fork_exec_wait`.
- [ ] `preempt_test` shows interleaved parent/child output; its two
      procs each complete all 10 iterations.
- [ ] `segv_test` exits cleanly: parent prints "waited", child
      status is non-zero, kernel does not panic.
- [ ] `yield_test` interleaves `p0 c0 p1 c1 …`.
- [ ] `sleep_test` pauses visibly between prints.
- [ ] Page-free count before `sched_init` and after a full round of
      spawn/reap cycles is identical (checked by the selftest).
- [ ] `trap.S` no longer reads `sstatus` from the CSR during exit
      after GPR restore. The two return paths (U/S) are explicit.
- [ ] `free_proc` calls `free_user_pgtable` when `pagetable != 0`.
- [ ] `scheduler_run`'s orphan-reap switches to `kernel_pgtable`
      before the free.
- [ ] `docs/SBUnix_Roadmap.md` §2 and the Phase 3 tail section are
      updated to check this off and link to this design doc.
- [ ] PR description lists every file touched and the new user
      binaries added to tarfs.
