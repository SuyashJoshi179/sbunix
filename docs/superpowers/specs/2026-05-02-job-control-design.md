# Job control: Job Control (Sessions, Process Groups, Foreground/Background)

**Status:** approved (2026-05-02)
**Branch:** `feature/job-control`
**Target:** POSIX-style job control end-to-end (Target C from brainstorm).

## Goal

Bring the SBUnix kernel and userspace from "no job control" to a working POSIX-ish job-control story: process groups, sessions, controlling terminal, foreground/background, stop/continue, signal-to-pgrp delivery, and a job-control-aware `/bin/sh` (`&`, `fg`, `bg`, `jobs`).

Out of scope: pseudo-terminals (ptys), multiple controlling terminals, real `SIGHUP` on hangup detection (we send SIGHUP only on session-leader exit / orphaned pgrp paths).

## Architecture

The kernel gains three new identity fields per process — `pgid`, `sid`, and a `last_signal` — and a new lifecycle state `PROC_STOPPED`. Signal delivery learns to route by pgrp (`kill(-pgid, sig)` and stop-char delivery from the tty) and to honour the new stop/continue actions (SIGTSTP, SIGTTIN, SIGTTOU, SIGCONT). The console driver's "foreground process" pointer becomes a "foreground pgrp" pointer keyed off the controlling-terminal session, set/queried via `tcsetpgrp`/`tcgetpgrp`. `wait4` replaces `wait` as the primitive, and grows `WUNTRACED`, `WCONTINUED`, `WNOHANG`. The shell becomes a job-control shell: it forks pipelines into their own pgrp, hands off the tty before waiting, reclaims the tty after, and tracks stopped/background jobs in a job table.

## Detailed Design

### 1. Kernel data model

`struct pcb` (in `kernel/include/proc.h`) gains:

```c
int pgid;           // process group id (== pid for group leader)
int sid;            // session id (== pid for session leader)
int last_signal;    // signal that last caused stop/term, for wait status
unsigned char stopped_reported;     // wait already reported the stop
unsigned char continued_pending;    // SIGCONT happened, wait should report
```

`proc_state_t` gains `PROC_STOPPED = 5`.

Per-session state lives outside the pcb on the controlling-tty side: `kernel/termios.c` keeps `static int console_fg_pgid;` (replaces `console_fg_pid`) and `static int console_session_sid;` (which session "owns" the console — set by `setsid` after the session leader opens the console and calls `tcsetpgrp`). For our single-tty kernel, only one session can own the console at a time; stealing isn't supported.

### 2. Inheritance rules

- `fork`: child inherits `pgid` and `sid` from parent.
- `exec`: pgid/sid preserved.
- `setpgid(pid, pgid)`:
  - `pid==0` means caller; `pgid==0` means use `pid`.
  - Caller must be in same session as target.
  - Target must not be a session leader (`pid == sid`).
  - If creating a new pgrp (`pgid == pid`), it's allowed unconditionally for self.
  - Target must not have already exec'd (POSIX restriction); we enforce only the "before exec by parent on child" common case by allowing both child-on-self-pre-exec and parent-on-child-pre-exec.
- `setsid()`: fails if caller is already a pgrp leader. Else: new session = new pgid = pid, no controlling tty (caller loses access to `tcsetpgrp` until reopened — we don't model "open tty without O_NOCTTY" yet, so `setsid` simply detaches; the next `tcsetpgrp` from a session leader claims the tty).

### 3. Signals

New defaults in `kernel/signal.c`:

| Signal  | # | Default action |
|---------|---|----------------|
| SIGHUP  | 1 | terminate |
| SIGTSTP | 20 | stop |
| SIGTTIN | 21 | stop |
| SIGTTOU | 22 | stop |
| SIGCONT | 18 | continue (no-op if not stopped) |

Stop = set `state = PROC_STOPPED`, save `last_signal = sig`, wake parent if it's `wait4`-ing with WUNTRACED.
Continue = if `state == PROC_STOPPED`, set `state = PROC_READY`, set `continued_pending = 1`, wake parent if it's `wait4`-ing with WCONTINUED. SIGCONT also unconditionally clears any pending stop signals.

`SIGSTOP` (already #19) joins the stop family with the same default; cannot be caught/blocked/ignored.

`kill(pid, sig)` extends:
- `pid > 0`: deliver to pid (existing).
- `pid == 0`: deliver to every member of caller's pgrp.
- `pid == -1`: not implemented (returns EPERM) — we don't have permissions to make this safe.
- `pid < -1`: deliver to every member of pgrp `-pid`.

Helper `send_signal_pgrp(int pgid, int sig)` in `kernel/signal.c` walks the proc table; called by both `kill(-pgid, ...)` and the tty stop-char path.

Signal delivery checkpoint stays the same — checked on return-to-user. PROC_STOPPED is honoured by the scheduler exactly like PROC_SLEEPING (skipped).

### 4. TTY layer

`kernel/drivers/uart.c` — `uart_rx_isr` adds:

- VSUSP (default 0x1A, ^Z) → `send_signal_pgrp(termios_get_fg_pgid(), SIGTSTP)`.
- VQUIT (default 0x1C, ^\) → `send_signal_pgrp(..., SIGQUIT)`.
- Existing VINTR is rerouted: instead of `send_signal_by_pid(termios_get_fg_pid(), SIGINT)`, call `send_signal_pgrp(termios_get_fg_pgid(), SIGINT)`.

`kernel/termios.c`:
- Replace `console_fg_pid` with `console_fg_pgid`. Old getter/setter renamed (`termios_get_fg_pgid`, `termios_set_fg_pgid`). Compatibility helpers gone — call sites updated.
- Add `console_session_sid` (assigned by `tcsetpgrp` from session leader, validated against caller's session).
- `tcsetpgrp` / `tcgetpgrp`: implemented via the existing `ioctl` path
  (`TIOCSPGRP` / `TIOCGPGRP`) on the console fd. The `TIOCSPGRP` handler
  validates that the caller's session matches `console_session_sid`
  (claiming it if unset) before writing the pgid. We deliberately reused
  the existing ioctl wire instead of adding dedicated syscalls — the
  semantics are the same.

`kernel/fs/devfs.c` — `console_read` SIGTTIN gate:
- If caller `p->pgid != console_fg_pgid` and caller's session owns the console:
  - If SIGTTIN is blocked or ignored in caller, return `-EIO` (POSIX behaviour).
  - Else `send_signal_pgrp(p->pgid, SIGTTIN)` and return `-EINTR` (signal handler will run on return-to-user; caller may retry).
- We do *not* enforce TTOU on writes initially (TOSTOP off by default). Hooks left for future.

### 5. Syscall surface

New entries in `kernel/include/syscall.h` and `kernel/syscall.c`:

```
SYS_setpgid    95   (pid, pgid)
SYS_getpgid    96   (pid)
SYS_getpgrp    97   ()
SYS_setsid     98   ()
SYS_getsid     99   (pid)
SYS_wait4     106   (pid, *status, options, *rusage)   // rusage ignored
```

`tcsetpgrp` / `tcgetpgrp` are *not* added as new syscalls — they go
through the pre-existing `ioctl(TIOCSPGRP / TIOCGPGRP)` path on the
console fd, which the kernel handler updates in lock-step with the
session/pgid bookkeeping. The shell's existing
`ioctl(0, TIOCSPGRP, &pid)` call site keeps working.

`SYS_wait` (7) keeps working — implemented as `wait4(-1, status, 0, NULL)` for back-compat; once shell is migrated we keep both.

### 6. wait4 status encoding

In `libc/include/sys/wait.h`:

```c
#define WNOHANG    1
#define WUNTRACED  2
#define WCONTINUED 8

#define WIFEXITED(s)    (((s) & 0xff) == 0)
#define WEXITSTATUS(s)  (((s) >> 8) & 0xff)
#define WIFSIGNALED(s)  (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f && ((s) & 0xff) != 0)
#define WTERMSIG(s)     ((s) & 0x7f)
#define WIFSTOPPED(s)   (((s) & 0xff) == 0x7f)
#define WSTOPSIG(s)     (((s) >> 8) & 0xff)
#define WIFCONTINUED(s) ((s) == 0xffff)
```

Kernel encodes status before returning from wait4.

### 7. Scheduler interaction

- PROC_STOPPED skipped (same skip path as PROC_SLEEPING).
- SIGCONT delivery sets `state = PROC_READY` and clears any pending stop signals (TSTP/TTIN/TTOU/STOP).
- A SIGCONT to a non-stopped process still sets `continued_pending = 1` but otherwise no-op (POSIX: continue is benign if running). Parent will see WIFCONTINUED if waiting with WCONTINUED — this matches Linux.
- Orphaned pgrp at session-leader exit (`exit_proc` for a process with `pid == sid` and the session has the controlling tty): for every pgrp in that session, send `SIGHUP` then `SIGCONT`. Implemented in `kernel/proc.c` exit path.

### 8. Shell (`/bin/sh`)

Job table:
```c
struct job {
    int   pgid;
    int   state;       // RUNNING, STOPPED, DONE
    char  cmd[128];    // joined argv
    int   id;          // %1, %2 …
    int   notify;      // pending status change to print at next prompt
};
#define MAX_JOBS 16
```

Behaviour:
- At startup: `shell_pgid = getpgrp(); tcsetpgrp(0, shell_pgid);` and ignore `SIGTTIN`, `SIGTTOU`, `SIGTSTP`, `SIGINT`, `SIGQUIT` so the shell itself doesn't die or stop on user-typed control chars.
- Pipeline launch:
  1. Parser tags pipeline with `bg` flag if final `&`.
  2. For first child: `fork`; in child `setpgid(0, 0)` and (if fg) `tcsetpgrp(0, getpid())`; in parent `setpgid(child, child)` (ignore EACCES if child already exec'd or set itself).
  3. For subsequent pipeline children: `setpgid(0, first_pid)` in both.
  4. Children also reset `SIGINT`/`SIGQUIT`/`SIGTSTP`/`SIGTTIN`/`SIGTTOU` to SIG_DFL before exec.
  5. fg parent: `wait4(-pgid, &st, WUNTRACED, NULL)` until all members terminate or whole pgrp is stopped (we treat the first WIFSTOPPED as "all stopped" since pipeline members usually share the stop signal). Then `tcsetpgrp(0, shell_pgid)`. If stopped, push job onto table.
  6. bg parent: assign job id, print `[id] pgid`.
- Builtins:
  - `jobs`: print table.
  - `fg [%n]`: `tcsetpgrp(0, job.pgid); kill(-job.pgid, SIGCONT); wait4(...)`. Reclaim tty.
  - `bg [%n]`: `kill(-job.pgid, SIGCONT)`; mark RUNNING.
- Between prompts: poll `wait4(-1, &st, WNOHANG | WUNTRACED | WCONTINUED, NULL)` to catch finished/stopped/continued jobs and print notifications.

### 9. Init

`bin/init/init.c` is the session leader. It must:
1. Call `setsid()` very early (right after `printf("init: starting")`).
2. After spawning each test child, do nothing extra (tests run synchronously, in their own pgrp set by exec? — we let them inherit init's session/pgrp; each test self-`setpgid` if it cares).
3. Around its long-running shell launch (the loop at the bottom):
   ```
   int pid = fork();
   if (pid == 0) {
       setpgid(0, 0);
       tcsetpgrp(0, getpid());
       execv("/bin/sh", 0);
   }
   setpgid(pid, pid);
   tcsetpgrp(0, pid);
   /* wait */
   tcsetpgrp(0, getpgrp());
   ```

### 10. Tests

All under `bin/`, registered in `bin/init/init.c` `tests[]` (insert before `/bin/sh_c_test` so they run in regular suite):

- `pgrp_test`: setpgid(0,0); assert getpgrp()==getpid(); fork child; child setpgid(0,0); parent reads getpgid(child); assert child's pgid is child pid.
- `setsid_test`: fork; child setsid(); assert getsid(0)==getpid()==getpgrp(); parent waits.
- `kill_pgrp_test`: parent setpgid(0,0); fork 3 children, each pause(); parent kill(-getpgrp(), SIGTERM); reap all 3, expect WIFSIGNALED && WTERMSIG==SIGTERM.
- `sigtstp_test`: fork; child raise(SIGTSTP); parent wait4(child, &st, WUNTRACED) → WIFSTOPPED && WSTOPSIG==SIGTSTP; parent kill(child, SIGCONT); wait4(child, &st, WCONTINUED) → WIFCONTINUED; parent kill(child, SIGTERM); reap.
- `sigttin_test`: fork; child setpgid(0,0); child read(0, ...); expect EINTR + SIGTTIN delivered (child catches it and exits 0); parent waits.
- `wait4_nohang_test`: fork; child sleep(200ms); parent wait4(-1, &st, WNOHANG) → 0 immediately; sleep 300ms; wait4(-1, &st, WNOHANG) → child pid + WIFEXITED.
- `jobctl_smoke_test` (best-effort): fork; child execv("/bin/sh", "-c", "sleep 1 & wait"); parent wait. Skipped if `sh -c` not present in this build; covered by manual smoke.

### 11. Risks / mitigations

- **Setpgid race:** parent and child both call setpgid; idempotent.
- **In-kernel locks while STOPPED:** we only enter STOPPED at signal-delivery checkpoints, which run with no kernel locks held.
- **Stale fg_pgid:** if fg pgrp's last member dies without shell reclaiming tty, fg_pgid points at a dead pgid; tty signal char becomes a no-op (`send_signal_pgrp` finds no members). Shell always reclaims after wait.
- **Console session ownership:** init claims via setsid + tcsetpgrp at boot; never released.
- **Test ordering:** new tests inserted before `usertests` so a regression there fails fast; `usertests` itself is unaffected because it doesn't depend on job control.

## Acceptance Criteria

1. `make qemu` runs to completion with the existing test suite green plus the six new tests.
2. Inside `/bin/sh`: typing `sleep 5 &` returns prompt immediately and prints `[1] <pgid>`.
3. Inside `/bin/sh`: typing `sleep 5` then `^Z` stops the job, prompt returns; `jobs` lists it stopped; `fg` resumes; `^C` interrupts; another `^Z` re-stops; `bg` resumes in background.
4. `kill -TERM -<pgid>` kills every member.
5. Background process attempting to read from console gets SIGTTIN.
