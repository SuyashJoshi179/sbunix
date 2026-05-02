# Job Control Implementation Plan

> **For agentic workers:** Steps use checkbox (`- [ ]`) syntax. Each task is a self-contained unit ending in a commit. Run `make qemu` (timeout ~120s) after every task that touches kernel or libc; abort if existing suite regresses.

**Goal:** Land job control per `docs/superpowers/specs/2026-05-02-job-control-design.md`.

**Architecture:** `pcb` gains pgid/sid/last_signal/PROC_STOPPED. Kernel adds setpgid/getpgid/getpgrp/setsid/getsid/wait4/tcsetpgrp/tcgetpgrp syscalls. signal.c gets SIGTSTP/TTIN/TTOU/CONT defaults and `kill(-pgid)`. uart.c routes ^Z/^\\ to fg pgrp. `/bin/sh` becomes job-control shell.

**Tech Stack:** RISC-V64 freestanding C, qemu-system-riscv64.

---

## Task 1: Per-process pgid/sid plumbing

**Files:**
- Modify: `kernel/include/proc.h`
- Modify: `kernel/proc.c` (alloc_proc init, fork inheritance)
- Modify: `kernel/exec.c` (preserve pgid/sid through proc_spawn — already preserved on fork; init's spawn must seed pgid=sid=pid)

- [ ] **Step 1: Add fields to pcb**

In `kernel/include/proc.h`, near `parent_pid`:
```c
int pgid;
int sid;
int last_signal;
unsigned char stopped_reported;
unsigned char continued_pending;
```
Extend `proc_state_t`:
```c
PROC_STOPPED = 5,
```

- [ ] **Step 2: Initialise in alloc_proc**

In `kernel/proc.c` `alloc_proc()`, after assigning pid:
```c
p->pgid = p->pid;
p->sid  = p->pid;
p->last_signal = 0;
p->stopped_reported = 0;
p->continued_pending = 0;
```

- [ ] **Step 3: Inherit on fork**

In `kernel/proc.c` `sys_fork`/`fork_proc` (whichever clones state), after copying parent fields:
```c
child->pgid = parent->pgid;
child->sid  = parent->sid;
child->last_signal = 0;
child->stopped_reported = 0;
child->continued_pending = 0;
```

- [ ] **Step 4: Build, run existing suite**

```
make qemu
```
Expect: same pass count as before (no behaviour change yet).

- [ ] **Step 5: Commit**

```
git add kernel/include/proc.h kernel/proc.c
git commit -m "kernel: add pgid/sid/last_signal/PROC_STOPPED to pcb"
```

---

## Task 2: setpgid/getpgid/getpgrp/setsid/getsid syscalls + libc

**Files:**
- Modify: `kernel/include/syscall.h`
- Modify: `kernel/syscall.c`
- Create: `kernel/proc_pgrp.c` (or extend `kernel/proc.c`)
- Modify: `libc/include/unistd.h`
- Modify: `libc/sys/syscalls.S` (or wherever syscall stubs live)

- [ ] **Step 1: Reserve syscall numbers**

In `kernel/include/syscall.h`:
```c
#define SYS_setpgid 95
#define SYS_getpgid 96
#define SYS_getpgrp 97
#define SYS_setsid  98
#define SYS_getsid  99
```

- [ ] **Step 2: Implement handlers**

In `kernel/proc.c` (or new section):
```c
int sys_setpgid(int pid, int pgid) {
    struct pcb *p = (pid == 0) ? curproc() : find_proc_by_pid(pid);
    if (!p) return -ESRCH;
    if (p->sid == p->pid) return -EPERM;          // session leader
    if (p->sid != curproc()->sid) return -EPERM;
    if (pgid == 0) pgid = p->pid;
    if (pgid != p->pid) {
        struct pcb *leader = find_proc_by_pid(pgid);
        if (!leader || leader->sid != curproc()->sid) return -EPERM;
    }
    p->pgid = pgid;
    return 0;
}
int sys_getpgid(int pid) {
    struct pcb *p = (pid == 0) ? curproc() : find_proc_by_pid(pid);
    if (!p) return -ESRCH;
    return p->pgid;
}
int sys_getpgrp(void) { return curproc()->pgid; }
int sys_setsid(void) {
    struct pcb *p = curproc();
    /* Refuse if already a pgrp leader of any other pgrp */
    for (int i = 0; i < NPROC; i++) {
        struct pcb *q = &proctab[i];
        if (q->state != PROC_UNUSED && q->pgid == p->pid && q != p) return -EPERM;
    }
    p->sid  = p->pid;
    p->pgid = p->pid;
    return p->sid;
}
int sys_getsid(int pid) {
    struct pcb *p = (pid == 0) ? curproc() : find_proc_by_pid(pid);
    if (!p) return -ESRCH;
    return p->sid;
}
```
Add `find_proc_by_pid` if missing. (Check `kernel/proc.c` for an existing equivalent — likely `proc_by_pid`. Use that name if present.)

- [ ] **Step 3: Wire into syscall dispatch**

In `kernel/syscall.c`, add cases.

- [ ] **Step 4: libc declarations**

In `libc/include/unistd.h` add:
```c
int setpgid(int pid, int pgid);
int getpgid(int pid);
int getpgrp(void);
int setsid(void);
int getsid(int pid);
```

- [ ] **Step 5: libc syscall stubs**

Match existing pattern in `libc/sys/` (find current `kill` or `getuid` stub for reference). Add 5 stubs.

- [ ] **Step 6: Test — pgrp_test**

Create `bin/pgrp_test/pgrp_test.c`:
```c
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
int main(void) {
    int pass = 0, fail = 0;
    if (getpgrp() == getpid()) pass++; else { printf("FAIL: getpgrp==pid\n"); fail++; }
    if (setpgid(0, 0) == 0) pass++; else { printf("FAIL: setpgid(0,0)\n"); fail++; }
    if (getpgid(0) == getpid()) pass++; else { printf("FAIL: getpgid(0)\n"); fail++; }
    int kid = fork();
    if (kid == 0) { setpgid(0, 0); _exit(0); }
    int st; wait(&st);
    printf("pgrp_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
```
Add `bin/pgrp_test/Makefile` (copy from `bin/wait_test/Makefile`, replace name).
Add to top-level `Makefile` USERBINS list (find pattern, add `pgrp_test`).
Add `"/bin/pgrp_test",` after the libc test block in `bin/init/init.c`.

- [ ] **Step 7: Run**

```
make qemu
```
Expect new test passes; total +1.

- [ ] **Step 8: Commit**

```
git add -A
git commit -m "kernel,libc: add setpgid/getpgid/getpgrp/setsid/getsid + pgrp_test"
```

---

## Task 3: kill(-pgid) and send_signal_pgrp

**Files:**
- Modify: `kernel/signal.c`

- [ ] **Step 1: Add helper**

In `kernel/signal.c`:
```c
int send_signal_pgrp(int pgid, int sig) {
    int hits = 0;
    for (int i = 0; i < NPROC; i++) {
        struct pcb *p = &proctab[i];
        if (p->state == PROC_UNUSED) continue;
        if (p->pgid != pgid) continue;
        if (send_signal_pcb(p, sig) == 0) hits++;
    }
    return hits ? 0 : -ESRCH;
}
```
(Refactor existing per-pid send into `send_signal_pcb` helper if not already.)

- [ ] **Step 2: Extend sys_kill**

```c
long sys_kill(int pid, int sig) {
    if (sig < 0 || sig >= NSIG) return -EINVAL;
    if (pid > 0)  return send_signal_by_pid(pid, sig);
    if (pid == 0) return send_signal_pgrp(curproc()->pgid, sig);
    if (pid == -1) return -EPERM;
    return send_signal_pgrp(-pid, sig);
}
```

- [ ] **Step 3: Test — kill_pgrp_test**

Create `bin/kill_pgrp_test/kill_pgrp_test.c`:
```c
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
int main(void) {
    setpgid(0, 0);
    int pgid = getpgrp();
    int kids[3];
    for (int i = 0; i < 3; i++) {
        int pid = fork();
        if (pid == 0) { pause(); _exit(99); }
        kids[i] = pid;
    }
    /* small delay so children are pause()-ing */
    for (volatile int i = 0; i < 100000; i++) {}
    kill(-pgid, SIGTERM);
    int got = 0;
    for (int i = 0; i < 3; i++) {
        int st;
        if (wait(&st) > 0 && WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM) got++;
    }
    printf("kill_pgrp_test: %d/3 killed by SIGTERM\n", got);
    return got == 3 ? 0 : 1;
}
```
Register in init.c + Makefile.

- [ ] **Step 4: Run + commit**
```
make qemu && git add -A && git commit -m "kernel: kill(-pgid) and send_signal_pgrp"
```

---

## Task 4: SIGTSTP/SIGCONT — PROC_STOPPED, scheduler, default actions

**Files:**
- Modify: `kernel/signal.c`
- Modify: `kernel/proc.c` (scheduler skip, exit handling)
- Modify: `kernel/include/signal.h` (add SIGTSTP=20, SIGTTIN=21, SIGTTOU=22)
- Modify: `libc/include/signal.h` mirror

- [ ] **Step 1: Add signal numbers**

`libc/include/signal.h` and `kernel/include/signal.h`:
```c
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
```
(SIGCONT=18 already, SIGSTOP=19 already.)

- [ ] **Step 2: Default actions**

In `kernel/signal.c` deliver path, before invoking handler default-terminate:
```c
if (sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU || sig == SIGSTOP) {
    if (p->sig_handlers[sig].sa_handler == SIG_DFL) {
        p->state = PROC_STOPPED;
        p->last_signal = sig;
        p->stopped_reported = 0;
        proc_wakeup(p->parent_pid_chan_for_wait);  // wake parent waiters
        return;
    }
    /* Else handler installed → invoke as usual */
}
if (sig == SIGCONT) {
    /* Always clears any pending stop sigs */
    p->sig_pending &= ~((1ULL<<SIGTSTP)|(1ULL<<SIGTTIN)|(1ULL<<SIGTTOU)|(1ULL<<SIGSTOP));
    if (p->state == PROC_STOPPED) {
        p->state = PROC_READY;
        p->continued_pending = 1;
        proc_wakeup_parent_waiters(p);
    }
    if (p->sig_handlers[SIGCONT].sa_handler == SIG_DFL) return;  // no further action
}
```
(Glue — adapt the wakeup names to what's actually in proc.c. Likely `proc_wakeup(&parent->wait_chan)` keyed on the parent's wait channel.)

- [ ] **Step 3: Scheduler skip**

In the scheduler loop, change `if (p->state != PROC_READY) continue;` (or equivalent) — already implicitly skips STOPPED since != READY. Verify by reading the loop; STOPPED must not be runnable. No change usually needed beyond confirming.

- [ ] **Step 4: SIGKILL/SIGCONT bypass**

Confirm `SIGKILL` and `SIGCONT` cannot be blocked or ignored — already handled at sigaction install / blocked-mask paths. SIGCONT delivery to STOPPED process must succeed even if blocked.

- [ ] **Step 5: Test — sigtstp_test (without WUNTRACED yet — checks only that STOPPED stops scheduling and SIGCONT resumes)**

Skipped this task — full sigtstp_test waits for Task 5 (wait4). Add a minimal probe:

`bin/sigtstp_test/sigtstp_test.c`:
```c
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
static volatile int cont_seen = 0;
static void on_cont(int sig) { (void)sig; cont_seen = 1; }
int main(void) {
    struct sigaction sa = {0};
    sa.sa_handler = on_cont;
    sigaction(SIGCONT, &sa, 0);
    int pid = fork();
    if (pid == 0) {
        raise(SIGTSTP);             /* should stop here */
        printf("child resumed\n");
        _exit(cont_seen ? 0 : 5);
    }
    /* give child time to stop */
    for (volatile int i = 0; i < 200000; i++) {}
    kill(pid, SIGCONT);
    int st; wait(&st);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : 1;
}
```
Register.

- [ ] **Step 6: Run + commit**
```
make qemu && git add -A && git commit -m "kernel: SIGTSTP/CONT defaults, PROC_STOPPED scheduling"
```

---

## Task 5: wait4 + WUNTRACED/WCONTINUED/WNOHANG

**Files:**
- Modify: `kernel/proc.c` (replace wait core with wait4 core)
- Modify: `kernel/syscall.c`
- Modify: `kernel/include/syscall.h`
- Modify: `libc/include/sys/wait.h`
- Modify: `libc/sys/*` add wait4 stub

- [ ] **Step 1: Reserve SYS_wait4 = 106**

In `kernel/include/syscall.h`.

- [ ] **Step 2: Implement sys_wait4**

```c
long sys_wait4(int pid, int *status, int options, void *rusage) {
    (void)rusage;
    struct pcb *me = curproc();
    while (1) {
        int found = 0;
        for (int i = 0; i < NPROC; i++) {
            struct pcb *c = &proctab[i];
            if (c->parent_pid != me->pid) continue;
            if (pid > 0 && c->pid != pid) continue;
            if (pid == 0 && c->pgid != me->pgid) continue;
            if (pid < -1 && c->pgid != -pid) continue;
            found = 1;
            if (c->state == PROC_ZOMBIE) {
                int st = encode_status_exit_or_signal(c);
                if (status) copyout_int(me->pagetable, status, st);
                int ret = c->pid;
                reap_zombie(c);
                return ret;
            }
            if ((options & WUNTRACED) && c->state == PROC_STOPPED && !c->stopped_reported) {
                c->stopped_reported = 1;
                int st = (c->last_signal << 8) | 0x7f;
                if (status) copyout_int(me->pagetable, status, st);
                return c->pid;
            }
            if ((options & WCONTINUED) && c->continued_pending) {
                c->continued_pending = 0;
                int st = 0xffff;
                if (status) copyout_int(me->pagetable, status, st);
                return c->pid;
            }
        }
        if (!found) return -ECHILD;
        if (options & WNOHANG) return 0;
        proc_sleep_chan(&me->wait_chan);     /* whatever channel existing wait uses */
        if (curproc()->sig_pending & ~curproc()->sig_blocked) return -EINTR;
    }
}
```
Helpers `encode_status_exit_or_signal` and `copyout_int` may already exist; if not, implement inline using existing copy-out path.

- [ ] **Step 3: SYS_wait back-compat**

`sys_wait(int *status)` becomes `return sys_wait4(-1, status, 0, NULL);`.

- [ ] **Step 4: libc**

`libc/include/sys/wait.h`:
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
int wait4(int pid, int *status, int options, void *rusage);
int waitpid(int pid, int *status, int options);
static inline int waitpid_inline(int pid, int *st, int o) { return wait4(pid, st, o, 0); }
```
Then `waitpid` is `return wait4(pid, status, options, 0);`.

- [ ] **Step 5: Update existing exit-status encoding**

Inspect `kernel/proc.c` exit path. Today `exit_status` likely stores raw int and `wait` returns it directly. Add encoding: on exit-by-signal, `c->exit_status = signum & 0x7f;` on `_exit(n)`, `c->exit_status = (n & 0xff) << 8;`. Tests using bare `wait(&st); st==0` for success still work because `WIFEXITED && WEXITSTATUS==0`.

- [ ] **Step 6: Update sigtstp_test for full coverage**

Replace previous body with the full version from spec §10.

- [ ] **Step 7: wait4_nohang_test**

Per spec §10.

- [ ] **Step 8: Run + commit**

```
make qemu && git add -A && git commit -m "kernel,libc: wait4 with WUNTRACED/WCONTINUED/WNOHANG"
```

**WARNING:** This task is the riskiest. Many existing tests inspect `wait(&st)` `st`. After this task:
- If any test asserted `st == 0` for success — still OK.
- If any test asserted `st == <signum>` for kill — must update to `WIFSIGNALED(st)&&WTERMSIG(st)==signum`. Search:
  ```
  grep -rn "wait(&" bin/ | xargs grep -l "status" 
  ```
  Audit each, fix as needed. Run grep:
  ```
  grep -rn "WIFSIGNALED\|exit_status\|status ==" bin/
  ```

---

## Task 6: tcsetpgrp/tcgetpgrp + tty pgrp routing

**Files:**
- Modify: `kernel/termios.c`
- Modify: `kernel/include/termios.h`
- Modify: `kernel/include/syscall.h` (114, 115)
- Modify: `kernel/syscall.c`
- Modify: `kernel/drivers/uart.c` (route via pgrp; add VSUSP, VQUIT)
- Modify: `kernel/fs/devfs.c` (SIGTTIN gate on console_read)
- Modify: `libc/include/unistd.h` (`tcsetpgrp`, `tcgetpgrp` decl)
- Modify: `libc/sys/*`
- Modify: `bin/sh/sh.c:185` — replace `ioctl(0, TIOCSPGRP, &pid)` with `tcsetpgrp(0, pid)`

- [ ] **Step 1: termios.c rewrite**

```c
static int console_fg_pgid;
static int console_session_sid;

void termios_init(void) {
    /* … existing tio init … */
    console_tio.c_cc[VINTR] = 0x03;
    console_tio.c_cc[VEOF]  = 0x04;
    console_tio.c_cc[VERASE]= 0x7f;
    console_tio.c_cc[VSUSP] = 0x1A;
    console_tio.c_cc[VQUIT] = 0x1C;
    console_fg_pgid = 0;
    console_session_sid = 0;
}
int  termios_get_fg_pgid(void) { return console_fg_pgid; }
void termios_set_fg_pgid(int pgid) { console_fg_pgid = pgid; }
int  termios_get_session(void) { return console_session_sid; }
void termios_set_session(int sid) { console_session_sid = sid; }
```

- [ ] **Step 2: Add VSUSP/VQUIT indices**

In `kernel/include/termios.h` (and libc copy if separate), ensure indices defined:
```c
#define VINTR  0
#define VQUIT  1
#define VERASE 2
#define VEOF   4
#define VSUSP  10
#define VMIN   16
#define VTIME  17
```
(Match existing layout; only add VSUSP/VQUIT if missing.)

- [ ] **Step 3: tcsetpgrp/tcgetpgrp syscalls**

```c
long sys_tcsetpgrp(int fd, int pgid) {
    struct file *f = curproc()->ofile[fd];
    if (!f || !is_console_file(f)) return -ENOTTY;
    int my_sid = curproc()->sid;
    int cur = termios_get_session();
    if (cur == 0) termios_set_session(my_sid);
    else if (cur != my_sid) return -EPERM;
    termios_set_fg_pgid(pgid);
    return 0;
}
long sys_tcgetpgrp(int fd) {
    struct file *f = curproc()->ofile[fd];
    if (!f || !is_console_file(f)) return -ENOTTY;
    return termios_get_fg_pgid();
}
```
Add `is_console_file` if missing — check `f->ip == devfs_console_inode()`.

- [ ] **Step 4: uart.c — VSUSP/VQUIT, pgrp delivery**

In `uart_rx_isr`, replace VINTR pid path and add stop chars:
```c
if ((tio.c_lflag & ISIG)) {
    int pgid = termios_get_fg_pgid();
    if (c == (char)tio.c_cc[VINTR]) {
        edit_len = 0;
        echo_caret('C');
        if (pgid > 0) send_signal_pgrp(pgid, SIGINT);
        continue;
    }
    if (c == (char)tio.c_cc[VQUIT]) {
        edit_len = 0;
        echo_caret('\\');
        if (pgid > 0) send_signal_pgrp(pgid, SIGQUIT);
        continue;
    }
    if (c == (char)tio.c_cc[VSUSP]) {
        edit_len = 0;
        echo_caret('Z');
        if (pgid > 0) send_signal_pgrp(pgid, SIGTSTP);
        continue;
    }
}
```
Where `echo_caret(ch)` writes `^ch\r\n` (extract from existing VINTR block).

- [ ] **Step 5: console_read SIGTTIN gate**

In `kernel/fs/devfs.c::console_read`:
```c
struct pcb *me = curproc();
int sess = termios_get_session();
int fg   = termios_get_fg_pgid();
if (sess && sess == me->sid && fg && fg != me->pgid) {
    if (sig_blocked_or_ignored(me, SIGTTIN)) return -EIO;
    send_signal_pgrp(me->pgid, SIGTTIN);
    return -EINTR;
}
```
Add `sig_blocked_or_ignored` — checks `(p->sig_blocked >> sig) & 1` or handler == SIG_IGN.

- [ ] **Step 6: Update sh.c**

Replace line 185 `ioctl(0, TIOCSPGRP, &pid);` with `tcsetpgrp(0, pid);`.

- [ ] **Step 7: libc stubs + unistd.h decls**

- [ ] **Step 8: sigttin_test**

Per spec §10. Register.

- [ ] **Step 9: Run + commit**
```
make qemu && git add -A && git commit -m "kernel: tcsetpgrp/tcgetpgrp + tty pgrp routing + SIGTTIN"
```

---

## Task 7: Init becomes session leader, hands off tty

**Files:**
- Modify: `bin/init/init.c`

- [ ] **Step 1: setsid early**

After `printf("init: starting")`:
```c
setsid();
tcsetpgrp(0, getpgrp());
```

- [ ] **Step 2: Per-test child pgrp**

In the test loop, after `if (pid == 0) {`:
```c
setpgid(0, 0);
```
(So Ctrl-C during a test only kills that test, not init.)
Parent: `setpgid(pid, pid);` after fork.
fg handover: `tcsetpgrp(0, pid);`
After wait: `tcsetpgrp(0, getpgrp());`

- [ ] **Step 3: Same wrapping around /bin/sh launch**

- [ ] **Step 4: Run + commit**
```
make qemu && git add -A && git commit -m "init: become session leader, hand tty off to children"
```

---

## Task 8: Job-control shell

**Files:**
- Modify: `bin/sh/sh.c`

- [ ] **Step 1: Job table struct + globals**
- [ ] **Step 2: Pipeline launch refactor — setpgid + tcsetpgrp dance**
- [ ] **Step 3: Parser: trailing `&` per pipeline**
- [ ] **Step 4: Builtins jobs/fg/bg**
- [ ] **Step 5: Inter-prompt SIGCHLD poll (`wait4(-1, &st, WNOHANG|WUNTRACED|WCONTINUED, 0)`)**
- [ ] **Step 6: Ignore TTIN/TTOU/TSTP/INT/QUIT in shell; reset to DFL in children before exec**

(Detailed code blocks deferred to execution time — many are local rewrites of the existing pipeline path.)

- [ ] **Step 7: Manual smoke per spec §11 acceptance criteria 2–5**

- [ ] **Step 8: Commit**
```
git add bin/sh/sh.c && git commit -m "sh: job control (& fg bg jobs, setpgid, tcsetpgrp)"
```

---

## Task 9: Orphaned-pgrp SIGHUP on session-leader exit

**Files:**
- Modify: `kernel/proc.c` (exit path)

- [ ] **Step 1:** In `exit_proc(p)`, if `p->pid == p->sid` and `termios_get_session() == p->sid`:
```c
for (each pcb q in same session, q != p) {
    send_signal_pcb(q, SIGHUP);
    send_signal_pcb(q, SIGCONT);
}
termios_set_session(0);
termios_set_fg_pgid(0);
```

- [ ] **Step 2:** No new test (covered manually). Run + commit.

---

## Task 10: Final pass

- [ ] Run final `make qemu` clean.
- [ ] `make qemu` clean run.
- [ ] Open PR via `git push -u origin feature/job-control` and print URL.

---

## Self-Review Notes

- **Spec coverage:** every numbered section in the spec has a task: §1→T1, §2→T2, §3→T3+T4, §4→T6, §5→T2+T5+T6, §6→T5, §7→T4, §8→T8, §9→T7, §10→tests scattered across T2/T3/T4/T5/T6, §11→T9.
- **Type consistency:** `termios_get_fg_pgid` (not `_pid`) used everywhere from T6 forward; `wait4` signature `(int pid, int *status, int options, void *rusage)` reused identically in libc and kernel.
- **Risk concentrations:** T5 (wait4) breaks any test that inspects raw status — explicit grep step included. T8 (shell) is large and unspec'd code — break into commits per builtin if needed.
