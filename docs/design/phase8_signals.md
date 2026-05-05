# Phase 8 — Signals, Time, Termios, uid/gid Stubs

**Status:** design ready, not yet implemented
**Depends on:** Phase 3 (trap entry, proc_exit, proc_sleep_chan), Phase 4 (inode ops vtable, /dev/console), Phase 6 (pipes for SIGPIPE, shell for Ctrl-C path), Phase 7 (user stack auto-grow — signal frame needs room)
**Enables:** Phase 9 (unified teardown must handle signal-kill path), Phase 10 (BusyBox applets that install signal handlers)

---

## 1. Goal

Give SBUnix just enough of the POSIX process model to make our own shell feel real:

- **Ctrl-C kills the foreground child; the shell survives.**
- `sleep 2 && echo done` works end-to-end under `/bin/sh`.
- `date` (a tiny new user binary) prints the wall-clock date/time
  read from the Goldfish RTC.
- User programs can install handlers (`sigaction`), block signals (`sigprocmask`), and send signals to other processes (`kill`).
- `clock_gettime`, `gettimeofday`, `nanosleep` exist and are tick-accurate.
- Processes running with a termios that has `ECHO` off don't echo input; with `ICANON` off, reads return per-byte.
- `getuid`/`getgid`/`setuid`/`setgid` etc. exist and always report 0 — not security, just so syscalls exist and programs that check them don't `-ENOSYS`-fail.

**Explicit non-goals:** job control (`&`, `fg`, `bg`, process groups proper), `siginfo_t`, `sigaltstack`, `SA_RESTART`, real-time signals (`SIGRTMIN..SIGRTMAX`), `sigtimedwait`, `sigsuspend` (optional), `pselect`/`ppoll`, post-1970 wall time, a real `/proc/<pid>/status`, and anything that needs a TLS slot. BusyBox `sh` and its job control are aspirational (Phase 10).

---

## 2. Preconditions

- Phase 7 landed: VMA list per PCB, stack VMA with auto-grow up to `MAX_STACK`, `user_fault` routes page faults through the VMA list.
- `proc_sleep_chan(chan)` / `proc_wakeup_chan(chan)` in `kernel/proc.c`.
- `/dev/console` inode with `read`/`write` ops in `kernel/fs/devfs.c`.
- `uart_rx_isr` in `kernel/drivers/uart.c` already does a crude canonical-mode line buffer and has Ctrl-C handling as a sentinel byte. Phase 8 replaces the sentinel with real signal delivery.
- `pipe_write` returns `-EPIPE` when the read end is closed. Phase 8 adds SIGPIPE semantics on top.
- `timer_ticks()` returns a monotonic tick counter at 100 ticks/sec.
- `uptr_ok(p)` user-pointer validator in `kernel/syscall.c`. Phase 8 adds `copyin_uptr` / `copyout_uptr` helpers for writing to user memory that may not be faulted in yet (sigframe delivery).

---

## 3. Concepts & data structures

### 3.1 Signal numbers

`kernel/include/signal.h` (mirror in `libc/include/signal.h`):

```c
#define NSIG    32

#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9    /* uncatchable */
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18    /* implemented as ignore */
#define SIGSTOP 19    /* uncatchable; in practice treated as kill */

typedef uint64_t sigset_t;   /* bit N -> signal N+1 */

typedef void (*sighandler_t)(int);
#define SIG_DFL  ((sighandler_t)0)
#define SIG_IGN  ((sighandler_t)1)

struct sigaction {
    sighandler_t sa_handler;
    sigset_t     sa_mask;
    int          sa_flags;    /* reserved; 0 for now */
};

/* sigprocmask how */
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
```

Bitmap layout: bit 0 unused, bit N = signal N. Gives us NSIG up to 63 trivially.

### 3.2 PCB additions

```c
/* struct pcb additions (kernel/include/proc.h) */

sigset_t        sig_pending;     /* bitmap of pending signals */
sigset_t        sig_blocked;     /* bitmap of currently-blocked signals */
sigset_t        sig_saved_mask;  /* mask to restore on sigreturn */
struct sigaction sig_handlers[NSIG];
uint8_t         in_sighandler;   /* 1 while a handler is running */
uint8_t         delivering_segv; /* SIGSEGV default-kill loop guard */
```

Invariants:
- `sig_blocked` **never** contains SIGKILL or SIGSTOP. Enforced at every write site.
- `sig_pending & ~sig_blocked` is the "deliverable now" set.
- Handlers set to `SIG_IGN` cause the pending bit to be cleared eagerly at delivery time (not on arrival — POSIX wording, and simpler for our code).

### 3.3 Signal frame on user stack

Layout written by the kernel on delivery, consumed by `sys_sigreturn`:

```c
#define SIGFRAME_MAGIC 0x5342534947464DULL  /* "SBSIGFRM" cookie, ASCII */

struct sigframe {
    uint64_t magic;                 /* SIGFRAME_MAGIC */
    uint64_t saved_mask;            /* pre-delivery sig_blocked */
    uint64_t saved_trapframe[36];   /* full 288-byte trapframe */
    uint8_t  trampoline[16];        /* optional: carry libc tramp addr for kernel to jump to */
};
/* sizeof = 320, aligned to 16 bytes */
```

- Written directly into the user address space (SUM bit set while writing).
- Frame size **must** be a multiple of 16 so that post-subtraction `sp` is 16-byte-aligned per the RISC-V ABI.
- `saved_trapframe[TF_A0]` holds the interrupted syscall's return value or user `a0`; on sigreturn we restore it verbatim, overwriting whatever the handler put in `a0`.

### 3.4 Default action table

```c
/* Deliberately const, indexed by signal number. */
enum { ACT_TERM, ACT_IGN, ACT_CORE };

static const uint8_t default_action[NSIG] = {
    [SIGHUP]=ACT_TERM, [SIGINT]=ACT_TERM, [SIGQUIT]=ACT_CORE,
    [SIGILL]=ACT_CORE, [SIGTRAP]=ACT_CORE, [SIGABRT]=ACT_CORE,
    [SIGBUS]=ACT_CORE, [SIGFPE]=ACT_CORE,  [SIGKILL]=ACT_TERM,
    [SIGUSR1]=ACT_TERM,[SIGSEGV]=ACT_CORE, [SIGUSR2]=ACT_TERM,
    [SIGPIPE]=ACT_TERM,[SIGALRM]=ACT_TERM, [SIGTERM]=ACT_TERM,
    [SIGCHLD]=ACT_IGN, [SIGCONT]=ACT_IGN,  [SIGSTOP]=ACT_TERM,
};
```

ACT_CORE is treated identically to ACT_TERM (no core dumps). Exit status for a default-kill is `128 + signum` to match POSIX so `$?` is useful later.

### 3.5 Time

```c
/* kernel/include/time.h */
struct timespec { int64_t tv_sec; int64_t tv_nsec; };
struct timeval  { int64_t tv_sec; int64_t tv_usec; };

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1
```

`CLOCK_MONOTONIC` is tick-based: `tv_sec = ticks/100`, `tv_nsec = (ticks % 100) * 10_000_000`. `CLOCK_REALTIME` and `gettimeofday` read nanoseconds since the Unix epoch from the Goldfish RTC (`drivers/rtc.c`, `rtc_read_ns`) and split into `tv_sec`/`tv_nsec` (or `tv_sec`/`tv_usec`). The two clocks are no longer aliased.

### 3.6 Termios

```c
/* kernel/include/termios.h */
struct termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_cc[32];
};

/* c_iflag */
#define ICRNL   0x00000100   /* map CR to NL on input */
/* c_oflag */
#define ONLCR   0x00000004   /* map NL to CR-NL on output */
/* c_lflag */
#define ISIG    0x00000001
#define ICANON  0x00000002
#define ECHO    0x00000008
/* c_cc indices */
#define VINTR    0
#define VEOF     4
#define VERASE   2
#define VMIN     6
#define VTIME    5

struct winsize {
    uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel;
};

/* ioctl numbers — Linux-compatible where trivial */
#define TCGETS      0x5401
#define TCSETS      0x5402
#define TIOCGWINSZ  0x5413
#define TIOCSPGRP   0x5410   /* we store a single int "console_fg_pid" */
#define TIOCGPGRP   0x540F
```

A single `static struct termios console_tio` lives in `kernel/termios.c` with the default at boot:

```c
console_tio.c_iflag = ICRNL;
console_tio.c_oflag = ONLCR;
console_tio.c_lflag = ISIG | ICANON | ECHO;
console_tio.c_cc[VINTR]  = 0x03;
console_tio.c_cc[VEOF]   = 0x04;
console_tio.c_cc[VERASE] = 0x7F;
```

A companion `static int console_fg_pid = 0;` tracks the foreground process for Ctrl-C delivery. The shell is responsible for keeping it current via `ioctl(0, TIOCSPGRP, &child_pid)` before fork/wait and resetting after.

### 3.7 uid/gid stubs

No data structure. All queries return 0; all setters accept and return 0 (not -EPERM — some programs check for success and bail otherwise). Files continue to be 0:0 in every stat result.

---

## 4. File-by-file changes

### 4.1 New files

- `kernel/include/signal.h` — constants, `struct sigaction`, `sigset_t`.
- `kernel/include/time.h` — `struct timespec`, `struct timeval`, clock IDs.
- `kernel/include/termios.h` — `struct termios`, flags, ioctl cmds, `struct winsize`.
- `kernel/signal.c` — `send_signal`, `check_signals`, sigframe build, default-action handling, all four signal syscalls.
- `kernel/termios.c` — the global termios struct, `console_fg_pid`, ioctl dispatch.
- `libc/include/signal.h` — mirror of kernel header.
- `libc/include/time.h`, `libc/include/sys/time.h` — mirrors.
- `libc/include/sys/types.h` — `uid_t`, `gid_t`, `pid_t`, `time_t`, `suseconds_t`.
- `libc/include/termios.h`, `libc/include/sys/ioctl.h` — wrappers.
- `libc/signal.c` — wrappers: `kill`, `sigaction`, `signal` (libc-flavored one-shot wrapper over sigaction), `sigprocmask`, `raise`, `pause`.
- `libc/time.c` — `clock_gettime`, `gettimeofday`, `nanosleep`, `time`.
- `libc/sigtramp.S` — 4-instruction trampoline used as the `ra` the handler returns to.
- `libc/ids.c` — `getuid`/`geteuid`/`getgid`/`getegid`/`setuid`/`setgid` wrappers (or fold into `libc/syscall.c`).
- `bin/date/date.c` — uptime printer.
- `bin/time_test/time_test.c`
- `bin/signal_test/signal_test.c`
- `bin/sigchld_test/sigchld_test.c`
- `bin/sigpipe_test/sigpipe_test.c`
- `bin/sigsegv_handler_test/sigsegv_handler_test.c`
- `bin/eintr_test/eintr_test.c`
- `bin/termios_test/termios_test.c`

### 4.2 Modified files

- `kernel/include/syscall.h` — new syscall numbers.
- `kernel/include/proc.h` — PCB signal fields.
- `kernel/include/errno.h` — add `ECHILD 10` (for `wait` when no children), already `EINTR`.
- `kernel/include/inode.h` — add `int (*ioctl)(struct inode *, int cmd, unsigned long arg);` to `struct inode_ops` (may return -ENOTTY for non-supporting inodes).
- `kernel/include/file.h` — declare `int fileioctl(struct file *f, int cmd, unsigned long arg);`.
- `kernel/syscall.c` — dispatch cases; update `sys_read`, `sys_write`, `sys_wait` to return `-EINTR` when interrupted.
- `kernel/trap.c` — after `trap_handler` finishes dispatch and is about to return, if returning to U-mode, call `check_signals(trapframe)`. Also replace `proc_exit_current(-14)` on U-mode fault with `send_signal(current, SIGSEGV)` and let the signal machinery apply the default action (which calls `proc_exit_current(128+11)` for us).
- `kernel/proc.c` — `proc_fork_current` copies handlers and blocked mask, **clears** pending in the child. `proc_exit_current` sends SIGCHLD to parent. `do_exec` resets non-SIG_IGN handlers to SIG_DFL.
- `kernel/pipe.c` — `pipe_write` sends SIGPIPE (not just returns `-EPIPE`) when the read end is closed. `pipe_read` and `pipe_write` return `-EINTR` if woken by a signal.
- `kernel/drivers/uart.c` — `uart_rx_isr` consults `console_tio`: honors `ECHO`, `ICANON`, `ISIG` on input. Ctrl-C (`c_cc[VINTR]`) with `ISIG` set delivers SIGINT to `console_fg_pid`. `uart_rx_get` returns `-EINTR` if woken by a signal.
- `kernel/fs/devfs.c` — wire `.ioctl = console_ioctl` into `console_ops`; implement `console_ioctl`.
- `kernel/file.c` — implement `fileioctl` that routes to `f->ip->ops->ioctl` for `FD_INODE`.
- `kernel/kernel.c` — call `termios_init()` at boot (between `devfs_init()` and `sched_init`).
- `libc/syscall.c`, `libc/include/unistd.h` — new wrappers.
- `libc/exit.c` — unchanged.
- `libc/crt.S` — unchanged (argc/argv already correct).
- `bin/sh/sh.c` — install SIGINT handler, `ioctl(TIOCSPGRP)` around forks, handle `-EINTR` on `read`.
- `bin/init/init.c` — append new tests to the test array.

---

## 5. Key flows

### 5.1 Sending a signal

`send_signal(struct pcb *target, int sig)`:
1. Reject if `target` is a kernel thread (`!target->is_user`).
2. Reject if `sig <= 0 || sig >= NSIG`.
3. If `sig == SIGKILL` or `sig == SIGSTOP`, forcibly clear from `sig_blocked` and from any `SIG_IGN` handler — these must always take effect.
4. Set bit: `target->sig_pending |= 1ULL << sig`.
5. If target is `PROC_SLEEPING` and `(sig_pending & ~sig_blocked) != 0`: set to `PROC_READY` so the sleeper wakes and its blocking syscall returns `-EINTR`.

Called from:
- `sys_kill` (user-initiated).
- `proc_exit_current` → SIGCHLD to parent.
- `pipe_write` on broken pipe → SIGPIPE to current.
- `uart_rx_isr` on VINTR char with ISIG → SIGINT to `console_fg_pid`.
- Trap handler on U-mode fault → SIGSEGV to current.

### 5.2 check_signals (return-to-user hook)

Called once at the end of `trap_handler`, only if returning to U-mode. Single call site.

```c
void check_signals(uint64_t *trapframe) {
    struct pcb *p = current_proc();
    if (!p || !p->is_user) return;

    /* Pick the lowest-numbered deliverable signal. */
    uint64_t deliverable = p->sig_pending & ~p->sig_blocked;
    if (deliverable == 0) return;

    int sig = __builtin_ctzll(deliverable);
    p->sig_pending &= ~(1ULL << sig);

    sighandler_t h = p->sig_handlers[sig].sa_handler;

    /* SIGKILL always kills, no matter what. */
    if (sig == SIGKILL || sig == SIGSTOP) {
        proc_exit_current(128 + sig);  /* does not return */
    }

    if (h == SIG_IGN) return;

    if (h == SIG_DFL) {
        if (default_action[sig] == ACT_IGN) return;
        /* ACT_TERM / ACT_CORE */
        if (sig == SIGSEGV && p->delivering_segv) {
            /* Second SEGV while delivering first — just kill now. */
        }
        proc_exit_current(128 + sig);
    }

    /* Custom handler — build sigframe on user stack. */
    build_sigframe_and_redirect(p, trapframe, sig, h);
}
```

### 5.3 Building the sigframe and redirecting to the handler

```c
static void build_sigframe_and_redirect(
        struct pcb *p, uint64_t *tf, int sig, sighandler_t h) {

    uint64_t user_sp = tf[1];                     /* x2 slot = user sp */
    uint64_t frame_addr = (user_sp - sizeof(struct sigframe)) & ~0xFULL;

    /* Validate: frame must land inside the stack VMA; if near the lower
       bound, vma_grow_stack will handle it via the fault path.  If the
       grow fails (MAX_STACK exceeded) -> default-kill with SIGSEGV. */
    if (!user_stack_room_for(p, frame_addr, sizeof(struct sigframe))) {
        p->delivering_segv = 1;
        proc_exit_current(128 + SIGSEGV);
    }

    /* copyout_uptr walks the user pagetable and faults in COW/lazy pages
       as needed. */
    struct sigframe fr;
    fr.magic        = SIGFRAME_MAGIC;
    fr.saved_mask   = p->sig_blocked;
    memcpy(fr.saved_trapframe, tf, 288);
    copyout_uptr(p->pagetable, frame_addr, &fr, sizeof(fr));

    /* Block the current signal (SA_NODEFER=0 default) + handler's own mask. */
    p->sig_saved_mask = p->sig_blocked;
    p->sig_blocked   |= (1ULL << sig) | p->sig_handlers[sig].sa_mask;
    p->sig_blocked   &= ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));

    /* Redirect return-to-user. */
    tf[TF_SEPC] = (uint64_t)h;
    tf[TF_A0]   = sig;                 /* handler(int signum) */
    tf[1]       = frame_addr;          /* new user sp */
    tf[TF_RA]   = SIGTRAMP_USER_ADDR;  /* where handler returns to */

    p->in_sighandler = 1;
}
```

`SIGTRAMP_USER_ADDR` is the address of `__sigtramp` in libc — already linked into every user binary, reachable at a stable VA because we have no ASLR.

### 5.4 Signal trampoline and sigreturn

`libc/sigtramp.S`:
```asm
.global __sigtramp
__sigtramp:
    li a7, SYS_sigreturn
    ecall
    ebreak                 /* should never reach here */
```

`sys_sigreturn`:
1. Compute `frame_addr = trapframe[1]` (the user sp left by the handler).
2. `copyin_uptr` a `struct sigframe` from that address.
3. Validate `fr.magic == SIGFRAME_MAGIC`. Otherwise `proc_exit_current(128+SIGSEGV)`.
4. Sanitize `fr.saved_trapframe[TF_SSTATUS]`: force SPP=0 and SPIE=1, mask off anything else.
5. Sanitize `fr.saved_trapframe[TF_SEPC]`: reject (kill) if `sepc >= KVMEM_OFFSET`.
6. `memcpy(trapframe, fr.saved_trapframe, 288);` — this is the return-to-user state.
7. Restore `p->sig_blocked = fr.saved_mask;` Clear SIGKILL/SIGSTOP bits.
8. `p->in_sighandler = 0;`
9. Return from the syscall **without** writing to `trapframe[TF_A0]`. The normal dispatch path does `trapframe[TF_A0] = ret`, which would clobber the restored a0 — so `sys_sigreturn` is a special case: the dispatch returns a sentinel like `SIGRETURN_SUCCESS = trapframe[TF_A0]` so the trap handler writes back what we just restored.

    Alternative (simpler): inline sigreturn dispatch directly in `syscall_dispatch`, and explicitly `return trapframe[TF_A0]` after the memcpy — trap handler's `trapframe[TF_A0] = ret` becomes a no-op.

### 5.5 Interrupted blocking syscalls

Every site that calls `proc_sleep` or `proc_sleep_chan` must re-check pending signals after it wakes.

```c
/* Template used by pipe_read, pipe_write, uart_rx_get, proc_wait_current */
for (;;) {
    if (resource_ready(...)) break;
    proc_sleep_chan(chan);
    if (has_pending_signal(current_proc())) return -EINTR;
}
```

Helper:
```c
static inline int has_pending_signal(struct pcb *p) {
    return (p->sig_pending & ~p->sig_blocked) != 0;
}
```

Returning `-EINTR` propagates back to user; `check_signals` then runs on the way out and delivers the handler. This two-step ordering (return -EINTR, *then* deliver handler) is correct: the user wrapper sees `-EINTR` in `errno` after control returns, which matches Linux.

### 5.6 Ctrl-C path end-to-end

1. User types `^C` on the console.
2. `uart_rx_isr` fires. With `console_tio.c_lflag & ISIG` and `c == c_cc[VINTR]`:
   - Flush edit buffer (discard partial line).
   - Echo `^C\r\n`.
   - Call `send_signal(pid_lookup(console_fg_pid), SIGINT)`.
   - (No sentinel byte into the line buffer any more.)
3. Foreground process wakes from whatever it was doing. Its `read`/`wait`/`nanosleep`/etc. returns `-EINTR`.
4. On return-to-user, `check_signals` delivers SIGINT.
5. If the fg proc is a child (e.g. `sleep 5`), it has no handler → default Term → `proc_exit_current(128+SIGINT)`.
6. Shell's `wait` returns the child's exit status. Shell resets `console_fg_pid = getpid()` via ioctl. Prompt is redrawn.

### 5.7 `nanosleep` and EINTR

```c
int64_t sys_nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!uptr_ok(req)) return -EFAULT;
    struct timespec k;
    copyin_uptr(..., &k, req, sizeof(k));
    if (k.tv_nsec < 0 || k.tv_nsec >= 1000000000) return -EINVAL;
    if (k.tv_sec < 0) return -EINVAL;

    uint64_t ms = (uint64_t)k.tv_sec * 1000ULL + (uint64_t)k.tv_nsec / 1000000ULL;
    uint64_t start = timer_ticks();
    uint64_t wake  = start + ms_to_ticks(ms);

    proc_sleep_until(wake);   /* like proc_sleep_ms but wake-point based */

    if (has_pending_signal(current_proc())) {
        uint64_t now = timer_ticks();
        uint64_t remaining_ms = (now >= wake) ? 0 : (wake - now) * 10;
        if (rem && uptr_ok(rem)) {
            struct timespec r = {
                .tv_sec  = remaining_ms / 1000,
                .tv_nsec = (remaining_ms % 1000) * 1000000
            };
            copyout_uptr(..., rem, &r, sizeof(r));
        }
        return -EINTR;
    }
    if (rem && uptr_ok(rem)) {
        struct timespec zero = {0, 0};
        copyout_uptr(..., rem, &zero, sizeof(zero));
    }
    return 0;
}
```

### 5.8 `ioctl` dispatch

```c
/* fd layer */
int fileioctl(struct file *f, int cmd, unsigned long arg) {
    if (!f) return -EBADF;
    if (f->type == FD_INODE && f->ip && f->ip->ops->ioctl)
        return f->ip->ops->ioctl(f->ip, cmd, arg);
    return -ENOTTY;
}

/* console */
static int console_ioctl(struct inode *ip, int cmd, unsigned long arg) {
    (void)ip;
    push_off();   /* block the RX ISR */
    int rc = -EINVAL;
    switch (cmd) {
    case TCGETS:
        if (!uptr_ok((void*)arg)) { rc = -EFAULT; break; }
        copyout_uptr(..., (void*)arg, &console_tio, sizeof(console_tio));
        rc = 0; break;
    case TCSETS:
        if (!uptr_ok((void*)arg)) { rc = -EFAULT; break; }
        copyin_uptr(..., &console_tio, (void*)arg, sizeof(console_tio));
        rc = 0; break;
    case TIOCGWINSZ: {
        struct winsize ws = { 24, 80, 0, 0 };
        if (!uptr_ok((void*)arg)) { rc = -EFAULT; break; }
        copyout_uptr(..., (void*)arg, &ws, sizeof(ws));
        rc = 0; break; }
    case TIOCSPGRP: {
        int pid;
        if (!uptr_ok((void*)arg)) { rc = -EFAULT; break; }
        copyin_uptr(..., &pid, (void*)arg, sizeof(pid));
        console_fg_pid = pid;
        rc = 0; break; }
    case TIOCGPGRP:
        if (!uptr_ok((void*)arg)) { rc = -EFAULT; break; }
        copyout_uptr(..., (void*)arg, &console_fg_pid, sizeof(int));
        rc = 0; break;
    }
    pop_off();
    return rc;
}
```

### 5.9 Revised `uart_rx_isr`

```c
void uart_rx_isr(void) {
    while (DR) {
        char c = RBR;

        if (console_tio.c_lflag & ISIG) {
            if (c == (char)console_tio.c_cc[VINTR]) {
                edit_len = 0;
                if (console_tio.c_lflag & ECHO) {
                    write_char('^'); write_char('C');
                    write_char('\r'); write_char('\n');
                }
                if (console_fg_pid)
                    send_signal_by_pid(console_fg_pid, SIGINT);
                continue;
            }
            /* VEOF: commit empty line = EOF */
            if (c == (char)console_tio.c_cc[VEOF] &&
                (console_tio.c_lflag & ICANON) && edit_len == 0) {
                line_commit_eof();
                continue;
            }
        }

        if (console_tio.c_iflag & ICRNL && c == '\r') c = '\n';

        if (console_tio.c_lflag & ICANON) {
            /* Canonical mode: existing editing + line-commit behavior */
            if (c == '\n') {
                if (console_tio.c_lflag & ECHO) {
                    write_char('\r'); write_char('\n');
                }
                line_commit();
            } else if (c == (char)console_tio.c_cc[VERASE] || c == '\b') {
                if (edit_len > 0) {
                    edit_len--;
                    if (console_tio.c_lflag & ECHO) {
                        write_char('\b'); write_char(' '); write_char('\b');
                    }
                }
            } else if (c >= 0x20 && edit_len < EDIT_SZ - 1) {
                edit_buf[edit_len++] = c;
                if (console_tio.c_lflag & ECHO) write_char(c);
            }
        } else {
            /* Raw mode: push each byte directly; wake reader. */
            line_push(&c, 1);
            if (console_tio.c_lflag & ECHO) write_char(c);
            if (rx_blocked_pid) {
                proc_wakeup(rx_blocked_pid);
                rx_blocked_pid = 0;
            }
        }
    }
}
```

---

## 6. Syscall ABI & error codes

### 6.1 New syscall numbers

```
SYS_clock_gettime  80    (clockid, struct timespec *ts)
SYS_gettimeofday   81    (struct timeval *tv, NULL)
SYS_nanosleep      82    (const struct timespec *req, struct timespec *rem)

SYS_kill           90    (pid, sig)
SYS_sigaction      91    (sig, const struct sigaction *act, struct sigaction *oldact)
SYS_sigprocmask    92    (how, const sigset_t *set, sigset_t *oldset)
SYS_sigreturn      93    ()
SYS_pause          94    ()

SYS_getuid        100
SYS_geteuid       101
SYS_getgid        102
SYS_getegid       103
SYS_setuid        104    (uid)
SYS_setgid        105    (gid)

SYS_ioctl         110    (fd, cmd, arg)
```

### 6.2 Return values and errnos

| Call | Success | Failure |
|---|---|---|
| `clock_gettime` | 0 | `-EINVAL` (bad clockid), `-EFAULT` |
| `gettimeofday` | 0 | `-EFAULT` |
| `nanosleep` | 0 | `-EINTR`, `-EINVAL`, `-EFAULT` |
| `kill` | 0 | `-ESRCH`, `-EINVAL` (bad sig or non-positive pid) |
| `sigaction` | 0 | `-EINVAL` (SIGKILL/SIGSTOP or bad sig), `-EFAULT` |
| `sigprocmask` | 0 | `-EINVAL`, `-EFAULT` |
| `sigreturn` | (restored a0) | kills the process on bad frame |
| `pause` | always `-EINTR` | — |
| `getuid` etc. | 0 | — |
| `setuid`/`setgid` | 0 | — (stub: always succeeds) |
| `ioctl` | 0 or driver-specific | `-EBADF`, `-ENOTTY`, `-EINVAL`, `-EFAULT` |

Add `ESRCH 3` and `ECHILD 10` to both `kernel/include/errno.h` and `libc/include/errno.h`.

Also: once signals exist, existing blocking syscalls gain a new failure code:
- `sys_read`, `sys_write` (on pipes), `sys_wait`, `sys_pause`, `sys_nanosleep` now all may return `-EINTR`.

---

## 7. Test plan

All user-space tests run from `init.c`'s test array; selftests are in `kernel/selftest.c`.

### 7.1 Kernel selftests (new `test_signal_*` and `test_time_*`)

- `test_signal_defaults`: a fresh PCB has `sig_pending == 0`, `sig_blocked == 0`, all handlers `SIG_DFL`.
- `test_signal_mask_enforce`: calling the internal helper that sets blocked mask with `1<<SIGKILL` or `1<<SIGSTOP` set silently clears those bits.
- `test_signal_pending_bitops`: set/clear/test of bits in `sig_pending`.
- `test_default_action_table`: every signal below NSIG has a defined entry.
- `test_termios_defaults`: `ISIG | ICANON | ECHO` at boot.
- `test_time_monotonic`: `timer_ticks()` advances; `clock_gettime_kernel(MONOTONIC)` produces tv_sec / tv_nsec within bounds.

### 7.2 User-space tests

- `bin/time_test`:
  - `clock_gettime` returns 0; `tv_nsec < 1e9`.
  - Two sequential calls yield monotonic non-decreasing ts.
  - `nanosleep({0, 50_000_000})` — sleep 50 ms — wakes with at least 50 ms elapsed per `clock_gettime`.
  - `nanosleep` with `tv_nsec = 1_000_000_000` returns `-EINVAL`.
- `bin/date`:
  - Prints `"up %lld.%03lld s\n"` — purely informational, status 0 on success.
- `bin/signal_test`:
  - `sigaction(SIGTERM, handler, NULL)`; `kill(getpid(), SIGTERM)`; verify handler ran (global flag).
  - `sigaction(SIGKILL, handler, NULL)` returns `-EINVAL`.
  - `sigprocmask(SIG_BLOCK, {SIGTERM})`; `kill(self, SIGTERM)`; handler does **not** run; `sigprocmask(SIG_UNBLOCK, {SIGTERM})`; handler runs.
  - `kill(getpid(), 0)` returns 0 (existence check).
  - `kill(99999, 0)` returns `-ESRCH`.
  - Nested signals: handler for SIGTERM raises SIGUSR1, SIGUSR1 handler increments counter → counter == 1.
- `bin/sigchld_test`:
  - Install SIGCHLD handler; `fork`+`exit(7)`; verify handler ran and `wait` returns pid and status 7.
- `bin/sigpipe_test`:
  - Pipe, close read end, write → default-kills writer with `128+SIGPIPE`.
  - Same setup with SIGPIPE caught: write returns `-EPIPE`, handler ran, process survives.
- `bin/sigsegv_handler_test`:
  - Install SIGSEGV handler; dereference NULL; handler fires; handler calls `_exit(0)`.
  - Without handler: child dereferences NULL → child exits `128+SIGSEGV`.
- `bin/eintr_test`:
  - Parent forks; child sleeps in `read(0, ...)` with no input queued; parent `kill(child, SIGTERM)` with an installed handler in the child; `read` returns `-EINTR`; child `_exit(42)`.
  - Same but `nanosleep(2 s)` interrupted after 200 ms; `rem` is > 1.7 s.
- `bin/termios_test`:
  - `TCGETS` returns defaults.
  - Modify `c_lflag &= ~ECHO`; `TCSETS`; write to console (no way to directly verify no-echo in automated test — just assert TCGETS/TCSETS round-trip).
  - Restore defaults.
  - `TIOCGWINSZ` returns `{24,80,0,0}`.
- `bin/uid_test` (tiny):
  - `getuid()==0`, `setuid(0)==0`, `setuid(42)==0`, `getuid()==0` (stub).

### 7.3 Manual / shell-interactive tests (not in `init`'s automated array — run after booting into `sh`)

- Type `^C` at an empty prompt — prompt redraws, shell survives.
- `sleep 5` then `^C` — `sleep` exits, shell survives, `$?` reflects 128+SIGINT (once we add `$?`).
- `sleep 2 && echo done` — after 2 s prints `done`.
- `yes | head -n 5` (if we have head/yes by then; if not, test equivalent).

---

## 8. Gotchas

This section complements the top-level roadmap gotchas — phase-specific traps to read **twice** before implementing.

1. **Signal delivery is a *single* call site.** `check_signals(trapframe)` goes at the tail of `trap_handler`, only when returning to U-mode (`SPP==0`). Never inside syscall bodies. Never in ISRs. Never in kernel threads. Getting this wrong produces subtle races where a handler runs on a kernel stack.

2. **Kernel must never fault on user memory while building the sigframe.** Lazy/COW/stack-auto-grow pages may not be present. Use `copyout_uptr` that walks the user pagetable explicitly and faults in missing pages. If stack-auto-grow fails (MAX_STACK hit) the delivery itself must fall back to default-kill SIGSEGV — and we need a one-shot guard (`p->delivering_segv`) so we don't recurse.

3. **16-byte stack alignment.** The RISC-V C ABI requires `sp % 16 == 0` at function entry. Size the sigframe to a multiple of 16 and mask `frame_addr &= ~0xFULL`. Off-by-8 will occasionally crash when the handler calls another function that pushes floats.

4. **`sstatus` leakage on sigreturn.** User could craft a sigframe that sets SPP=1 or SIE=0. Sanitize on sigreturn: force SPP=0, SPIE=1, and clear every bit outside a known-safe mask.

5. **`sepc` validation on sigreturn.** Reject any sepc `>= KVMEM_OFFSET`. Otherwise a malicious user can use sigreturn as a "jump into the kernel" primitive.

6. **`trapframe[TF_A0]` clobber on sigreturn return.** Normal syscall dispatch writes `trapframe[TF_A0] = ret`. Sigreturn's whole *purpose* is to restore the interrupted `a0`. Fix by either:
   (a) inlining sigreturn dispatch in `syscall_dispatch` and returning `trapframe[TF_A0]` (so the write-back is a no-op), or
   (b) adding a `SIGRETURN_NO_RETURN_WRITE` sentinel the dispatcher special-cases. Pick (a) — simpler.

7. **SIGKILL/SIGSTOP enforcement in three places.** sigaction rejects them; sigprocmask silently clears them; send_signal forcibly deblocks them. Any one of these missing is a hang or a privilege escalation.

8. **Pending cleared on fork, preserved on exec; handlers copied on fork, reset on exec (except SIG_IGN).** Easy to get backwards. Precedent: Linux `man 2 fork` and `man 2 execve`.

9. **SIGCHLD default is Ign, not Term.** A parent that doesn't install a SIGCHLD handler still gets `wait()` to work (we scan the proc list directly). Don't auto-reap unless we later add `SA_NOCLDWAIT`.

10. **Signal to a kernel thread is a no-op.** `send_signal` must reject `!is_user`. Our scheduler has kernel threads for nothing at the moment, but this will matter once there's an idle thread or worker.

11. **`kill(pid, 0)`** is the existence check. Still runs the permission check (we have no perms, so always succeeds if pid exists, else `-ESRCH`).

12. **Negative / zero pids = `-EINVAL`.** POSIX uses these for process groups and we don't have them. Don't silently accept.

13. **Signal delivery order.** Scan low-to-high bit. Not POSIX-strict but deterministic. Document that SIGINT (2) outranks SIGTERM (15) etc. only for *our* kernel; don't rely on ordering in user code.

14. **`-EINTR` vs `SA_RESTART`.** We pick `-EINTR` always. No restart. Libc wrappers do *not* retry. This matches BSD default and is simpler. Document in `signal.h`.

15. **`pause()` is always `-EINTR`.** The only way out of pause is a signal. If a handler caught it, pause still returns -EINTR after the handler runs.

16. **Async-signal-safety.** Our libc `printf` uses a buffer; calling it from a handler can corrupt state (we're single-threaded per process, but the handler is effectively re-entrant with the mainline). Document in `signal.h`: "only `write()` and `_exit()` are signal-safe."

17. **Console foreground pid is a cheat.** A single global `console_fg_pid`. The shell sets it around fork/wait via `ioctl(TIOCSPGRP)`. If the shell forgets, Ctrl-C goes nowhere (or worse, to the previous child's pid reused). The shell change is small — but getting it wrong silently breaks Ctrl-C. Consider adding a kernel-side fallback: "if `console_fg_pid` points at a dead/reused pid, deliver to the shell's parent instead."

18. **Termios struct layout is ABI.** Kernel and libc headers must match exactly. Write both at the same time. Once a user binary compiles against the libc version, never reorder fields.

19. **`TCSETS` is atomic w.r.t. the ISR.** Do the memcpy under `push_off/pop_off` so the ISR never sees a half-updated struct.

20. **`uart_rx_isr` runs with IRQs off.** That's the *entire* concurrency story on 1 hart. Any shared state (console_tio, console_fg_pid, line ring) read by a syscall must be under `push_off/pop_off`.

21. **ICANON-off (raw) mode drain.** When a program flips `ICANON` off, the line_buf might still hold a committed (but unconsumed) line from canonical mode. Next `read` will pick it up character-by-character via the raw path — design choice; document.

22. **`ECHO` off must also suppress backspace-erase echoing.** The current Phase 4 RX code writes `\b \b` unconditionally on backspace. Gate this on `ECHO`.

23. **`ONLCR` on output.** Currently `write_char` sends bytes raw. With `ONLCR` (default), `\n` written to console should emit `\r\n`. Implement in `console_write` (devfs) or leave as-is and document. Leave as-is for now — tools like less that want pure raw output shouldn't see our `\r\n` either; our shell already writes `\r\n` explicitly when it needs to.

24. **`CLOCK_REALTIME` source.** Backed by the Goldfish RTC, so `time(NULL)` returns true wall-clock seconds since 1970-01-01 UTC. `CLOCK_MONOTONIC` stays tick-based. Earlier drafts of this doc had REALTIME aliased to MONOTONIC starting at epoch 0; that aliasing has been removed.

25. **`ticks * 10ms` overflow.** At 10 ms/tick, a 64-bit tick counter overflows in ~5.8 billion years. `tv_sec = ticks/100` is fine for any realistic uptime. No worry.

26. **`nanosleep` granularity is 10 ms.** A user who asks for `{0, 1}` ns sleeps until the next tick. Document.

27. **`setuid(0)` must succeed in the stub.** Some programs (e.g. future busybox) check for success of `setuid(getuid())` at startup. Don't return `-EPERM`. Return 0.

28. **`fstat` still reports uid/gid = 0.** We already do this. Don't drift in a later phase.

29. **Re-entry to `check_signals`.** If a handler calls a syscall, that syscall returns through `trap_handler`, which calls `check_signals` again. Any *new* pending signal (different bit) gets delivered nested — correct POSIX behavior, and our code naturally supports it because the blocked mask was updated before jumping into the first handler. No extra work needed, but worth adding an explicit test.

30. **Test harness in `init.c`.** Some Phase 8 tests (eintr_test, sigchld_test) need the kernel scheduler to run multiple procs concurrently. Our existing `init.c` fork-wait loop already handles this — just add the test names.

---

## 9. Open questions (deliberately deferred)

- **Real process groups / session IDs.** `TIOCSPGRP` and `TIOCGPGRP` store a single int; no group abstraction. Any program that expects pgrp semantics (BusyBox `sh` job control) will need a Phase 9 or Phase 10 revisit.
- **`siginfo_t` (`SA_SIGINFO`).** Out of scope. Handler signature is plain `void (*)(int)`.
- **`sigaltstack`.** Handlers run on the regular user stack. Big stack-blowing handler and SIGSEGV-in-SIGSEGV are not supported beyond the delivering_segv guard.
- **Real-time signals (`SIGRTMIN..SIGRTMAX`).** NSIG=32 hard-coded.
- **`wait4` / `waitpid` / `WNOHANG`.** Current `wait` blocks unconditionally and reaps any child. Add in Phase 9 alongside resource cleanup.
- **`kill(-pgrp, sig)` and `kill(-1, sig)`.** Rejected with `-EINVAL` for now.
- **Signal-safe `printf`.** Too much work; document the limitation.
- **`select` / `poll` / `pselect`.** Whole new machinery. Not in this phase.
- **Per-process `errno`.** MicroPython (Phase 7.5) wants `__errno_location()`. We add it there, not here.
- **UART output `ONLCR`.** Low priority.
- **Timer IRQ-driven `SIGALRM`.** Would need a per-process alarm field and timer_handler scan. Add if Phase 10 demands it.

---

## 10. Exit criteria

- [ ] All new kernel selftests pass (signal defaults, termios defaults, time monotonic).
- [ ] `bin/time_test` passes: monotonic clock, nanosleep timing, bounds checking.
- [ ] `bin/date` prints wall-clock date/time read from the Goldfish RTC.
- [ ] `bin/signal_test` passes: kill, sigaction, sigprocmask, SIGKILL uncatchable, nested signals.
- [ ] `bin/sigchld_test` passes: parent gets SIGCHLD on child exit.
- [ ] `bin/sigpipe_test` passes: default-kill and caught cases.
- [ ] `bin/sigsegv_handler_test` passes: caught SIGSEGV; re-fault default-kills.
- [ ] `bin/eintr_test` passes: read/wait/nanosleep return -EINTR on signal.
- [ ] `bin/termios_test` passes: TCGETS/TCSETS round-trip, TIOCGWINSZ.
- [ ] `bin/uid_test` passes: stubs return 0.
- [ ] Manual: Ctrl-C in the shell terminates the foreground child and the shell survives and redraws the prompt.
- [ ] Manual: `sh> sleep 2 && echo done` works (requires `&&` in shell — note if deferred).
- [ ] `test_leak_spawn_free` still passes (no page leaks introduced).
- [ ] No new kernel panics on any syscall path tested in `init`'s array.

---

## 11. Sub-PR ordering and sizing

| Sub-PR | What ships | Rough size |
|---|---|---|
| 8a | Time & uid/gid stubs, `date`, `time_test` | ~200 LOC (kernel + libc + tests) |
| 8b | Signal core: kill/sigaction/sigprocmask/sigreturn, default actions, fork/exec integration, -EINTR on blocking syscalls, SIGSEGV on fault, SIGPIPE on broken pipe, SIGCHLD on exit, kernel selftests, 5 user-space tests | ~800 LOC |
| 8c | ioctl + inode_ops.ioctl, termios struct, console_ioctl, rewritten uart_rx_isr honoring ISIG/ICANON/ECHO, Ctrl-C → SIGINT to console_fg_pid, termios_test | ~400 LOC |
| 8d | Shell SIGINT handler + TIOCSPGRP around forks + -EINTR on read, `kill` built-in, init tests wired up, docs updated | ~100 LOC |

Ship strictly in order. Each PR is independently testable and bisectable. If Phase 8 runs long, 8a and 8b alone are enough to unblock Phase 9; 8c/8d can slip to a follow-up without blocking anything except the user-visible shell experience.
