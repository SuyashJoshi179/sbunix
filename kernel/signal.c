#include <signal.h>
#include <proc.h>
#include <vmem.h>
#include <pmem.h>
#include <string.h>
#include <printk.h>
#include <errno.h>
#include <syscall.h>
#include <riscv.h>

/* Default action table indexed by signal number (0 unused). */
static const uint8_t default_action[NSIG] = {
    [SIGHUP]  = ACT_TERM, [SIGINT]  = ACT_TERM, [SIGQUIT] = ACT_CORE,
    [SIGILL]  = ACT_CORE, [SIGTRAP] = ACT_CORE,  [SIGABRT] = ACT_CORE,
    [SIGBUS]  = ACT_CORE, [SIGFPE]  = ACT_CORE,  [SIGKILL] = ACT_TERM,
    [SIGUSR1] = ACT_TERM, [SIGSEGV] = ACT_CORE,  [SIGUSR2] = ACT_TERM,
    [SIGPIPE] = ACT_TERM, [SIGALRM] = ACT_TERM,  [SIGTERM] = ACT_TERM,
    [SIGCHLD] = ACT_IGN,  [SIGCONT] = ACT_CONT,  [SIGSTOP] = ACT_STOP,
    [SIGTSTP] = ACT_STOP, [SIGTTIN] = ACT_STOP,  [SIGTTOU] = ACT_STOP,
};

/* ----------------------------------------------------------------
 * send_signal — set a signal pending on a process.
 * ---------------------------------------------------------------- */
void send_signal(struct pcb *target, int sig) {
    if (!target || !target->is_user) return;
    if (sig <= 0 || sig >= NSIG) return;

    /* SIGKILL/SIGSTOP: force-unblock and override any SIG_IGN handler. */
    if (sig == SIGKILL || sig == SIGSTOP) {
        target->sig_blocked &= ~(1ULL << sig);
        target->sig_handlers[sig].sa_handler = SIG_DFL;
    }

    /* SIGCONT clears any pending stop signals (POSIX) and resumes a
     * stopped process even if SIGCONT itself is blocked or ignored.
     * If the target is not stopped and there is no user handler for
     * SIGCONT, treat as default-ignore so we don't wake a timer-sleep
     * via the "deliverable signal" wake check below. */
    if (sig == SIGCONT) {
        target->sig_pending &= ~((1ULL << SIGSTOP) | (1ULL << SIGTSTP) |
                                 (1ULL << SIGTTIN) | (1ULL << SIGTTOU));
        if (target->state == PROC_STOPPED) {
            target->state = PROC_READY;
            target->wake_tick = 0;
            target->continued_pending = 1;
            /* Wake parent waiting in wait4(WCONTINUED). */
            send_signal_by_pid(target->parent_pid, SIGCHLD);
            proc_wakeup(target->parent_pid);
        }
        if (target->sig_handlers[SIGCONT].sa_handler == SIG_DFL ||
            target->sig_handlers[SIGCONT].sa_handler == SIG_IGN) {
            return;
        }
    }

    /* Stop-signal arriving on a stopped process: discard (POSIX: SIGCONT
     * already drained these; another stop while stopped is meaningless). */
    if ((sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU || sig == SIGSTOP) &&
        target->state == PROC_STOPPED) {
        return;
    }

    target->sig_pending |= (1ULL << sig);

    /* SIGKILL must terminate a stopped process. Without this, kill -KILL
     * on a Ctrl-Z'd job hangs forever (the stopped proc never runs
     * check_signals to notice the pending kill). Force it back to READY
     * so the scheduler picks it up and the default ACT_TERM action runs. */
    if (sig == SIGKILL && target->state == PROC_STOPPED) {
        target->state = PROC_READY;
        target->wake_tick = 0;
        /* Notify parent that the stopped child is moving (will exit
         * shortly via check_signals → proc_exit_current). */
        send_signal_by_pid(target->parent_pid, SIGCHLD);
    }

    /* Wake a sleeping process if the signal is now deliverable. */
    if (target->state == PROC_SLEEPING &&
        sig_has_pending(target->sig_pending, target->sig_blocked)) {
        target->state    = PROC_READY;
        target->wake_tick = 0;
    }
}

void send_signal_by_pid(int pid, int sig) {
    for (struct pcb *p = proc_list_head(); p; p = p->next) {
        if (p->pid == pid) {
            send_signal(p, sig);
            return;
        }
    }
}

int send_signal_pgrp(int pgid, int sig) {
    int hits = 0;
    for (struct pcb *p = proc_list_head(); p; p = p->next) {
        if (p->state == PROC_UNUSED) continue;
        if (p->pgid != pgid) continue;
        send_signal(p, sig);
        hits++;
    }
    return hits;
}

/* True if the signal would have no observable effect on a running
 * process: explicit SIG_IGN, default-ignore, or default-continue. */
static int sig_is_no_op(struct pcb *p, int i) {
    sighandler_t h = p->sig_handlers[i].sa_handler;
    if (h == SIG_IGN) return 1;
    if (h == SIG_DFL) {
        uint8_t act = default_action[i];
        if (act == ACT_IGN || act == ACT_CONT) return 1;
    }
    return 0;
}

/* Drop any pending signals that are currently deliverable and that
 * pick_actionable would consider no-ops (explicit SIG_IGN,
 * default-ignore, or default-continue), then return the
 * lowest-numbered remaining deliverable signal, or 0. Used by both the
 * SA_RESTART picker in check_signals_after_syscall and the actual
 * delivery in check_signals so they always agree on which signal will
 * run. */
static int pick_actionable(struct pcb *p) {
    uint64_t deliverable = p->sig_pending & ~p->sig_blocked;
    int found = 0;
    for (int i = 1; i < NSIG; i++) {
        if (!(deliverable & (1ULL << i))) continue;
        if (sig_is_no_op(p, i)) {
            p->sig_pending &= ~(1ULL << i);
        } else if (!found) {
            found = i;
        }
    }
    return found;
}

int sig_has_actionable(struct pcb *p) {
    if (!p) return 0;
    uint64_t deliverable = p->sig_pending & ~p->sig_blocked;
    if (!deliverable) return 0;

    for (int i = 1; i < NSIG; i++) {
        if (!(deliverable & (1ULL << i))) continue;
        if (sig_is_no_op(p, i)) continue;
        return 1;
    }
    return 0;
}

/* ----------------------------------------------------------------
 * build_sigframe_and_redirect — push sigframe onto user stack,
 * redirect sepc to the handler.
 * ---------------------------------------------------------------- */
static void build_sigframe_and_redirect(struct pcb *p, uint64_t *tf,
                                        int sig, sighandler_t h) {
    /* tf[1] = x2 = user sp saved at entry */
    uint64_t user_sp  = tf[1];
    uint64_t frame_va = (user_sp - sizeof(struct sigframe)) & ~0xFULL;

    /* Sanity: frame must be in user VA range. */
    if (frame_va < PAGE_SIZE || frame_va + sizeof(struct sigframe) > KVMEM_OFFSET) {
        p->delivering_segv = 1;
        proc_exit_current(SIGSEGV & 0x7f);
    }

    /* Build the frame in kernel memory, then copyout. */
    struct sigframe fr;
    memset(&fr, 0, sizeof(fr));
    fr.magic      = SIGFRAME_MAGIC;
    /* If we got here via sigsuspend, the mask sigreturn must restore is the
     * caller's mask from before sigsuspend (POSIX), not the temporary
     * suspend mask currently in p->sig_blocked. */
    fr.saved_mask = p->sig_suspend_active ? p->sig_suspend_saved_mask
                                          : p->sig_blocked;
    p->sig_suspend_active = 0;
    memcpy(fr.saved_trapframe, tf, 288);

    /* Write frame to user stack — faults in lazy/COW pages as needed. */
    if (copyout((void *)frame_va, &fr, sizeof(fr)) < 0) {
        p->delivering_segv = 1;
        proc_exit_current(SIGSEGV & 0x7f);
    }

    /* Block current signal + handler's extra mask for the handler's duration. */
    p->sig_saved_mask = p->sig_blocked;
    p->sig_blocked   |= (1ULL << sig) | p->sig_handlers[sig].sa_mask;
    p->sig_blocked   &= ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));

    /* Redirect return-to-user: jump to handler, ra → sa_restorer (__sigtramp). */
    uint64_t restorer = (uint64_t)p->sig_handlers[sig].sa_restorer;
    tf[TF_SEPC] = (uint64_t)h;
    tf[TF_A0]   = (uint64_t)sig;
    tf[1]       = frame_va;
    tf[TF_RA]   = restorer ? restorer : 0;

    p->in_sighandler = 1;
}

/* ----------------------------------------------------------------
 * check_signals — called at end of trap_handler when returning to
 * U-mode.  Delivers one pending signal per trap return.
 * ---------------------------------------------------------------- */
void check_signals(uint64_t *trapframe) {
    struct pcb *p = current_proc();
    if (!p || !p->is_user) return;

    int sig = pick_actionable(p);
    if (sig == 0) {
        /* Nothing actionable (deliverable was empty, or only ignored
         * signals were pending and pick_actionable just discarded
         * them). Still restore the caller's mask if sigsuspend was
         * active so we satisfy POSIX. */
        if (p->sig_suspend_active) {
            p->sig_blocked = p->sig_suspend_saved_mask;
            p->sig_suspend_active = 0;
        }
        return;
    }

    p->sig_pending &= ~(1ULL << sig);

    /* SIGKILL: always kill, unblockable, uncatchable. */
    if (sig == SIGKILL) {
        proc_exit_current(sig & 0x7f);
    }

    /* SIGSTOP: stop unconditionally — cannot be caught or ignored. */
    if (sig == SIGSTOP) {
        p->state = PROC_STOPPED;
        p->last_signal = sig;
        p->stopped_reported = 0;
        send_signal_by_pid(p->parent_pid, SIGCHLD);
        proc_wakeup(p->parent_pid);
        proc_stop_current();
        return;
    }

    sighandler_t h = p->sig_handlers[sig].sa_handler;

    /* pick_actionable filters SIG_IGN / default-IGN / default-CONT, so
     * only SIG_DFL with TERM/CORE/STOP semantics or a custom handler
     * can reach here. */
    if (h == SIG_DFL) {
        uint8_t act = (sig < NSIG) ? default_action[sig] : ACT_TERM;
        if (act == ACT_STOP) {
            p->state = PROC_STOPPED;
            p->last_signal = sig;
            p->stopped_reported = 0;
            send_signal_by_pid(p->parent_pid, SIGCHLD);
            proc_wakeup(p->parent_pid);
            proc_stop_current();
            return;
        }
        if (sig == SIGSEGV && p->delivering_segv) {
            /* Recursive SIGSEGV — just die. */
        }
        proc_exit_current(sig & 0x7f);
    }

    /* Custom handler — redirect return-to-user into handler. */
    build_sigframe_and_redirect(p, trapframe, sig, h);
}

void check_signals_after_syscall(uint64_t *trapframe, int64_t ret,
                                 uint64_t orig_a0) {
    struct pcb *p = current_proc();
    if (!p || !p->is_user) {
        check_signals(trapframe);
        return;
    }

    /* Default policy for the kernel-internal -ERESTARTSYS sentinel: surface
     * EINTR to userspace. The SA_RESTART branch below overrides this by
     * rewinding sepc instead, so the syscall replays after the handler. */
    if (ret == -ERESTARTSYS) {
        trapframe[TF_A0] = (uint64_t)(int64_t)-EINTR;

        /* Use the same picker that check_signals will use, so the signal
         * we rewind sepc for is the one whose handler actually runs on
         * this trap return. pick_actionable also drops no-op signals
         * from sig_pending, which is what stops them from re-selecting
         * on a subsequent replay. */
        int sig = pick_actionable(p);
        if (sig) {
            sighandler_t h = p->sig_handlers[sig].sa_handler;
            int has_custom  = (h != SIG_DFL && h != SIG_IGN);
            int restart_set = (p->sig_handlers[sig].sa_flags & SA_RESTART) != 0;
            if (has_custom && restart_set) {
                /* Rewind to the ecall instruction. Other syscall
                 * argument registers (a1..a6, a7) are untouched in
                 * the trapframe after entry, so only a0 needs to be
                 * reset to its pre-dispatch value. The handler runs
                 * with this rewound trapframe saved in the sigframe;
                 * sigreturn restores it and the ecall replays. */
                trapframe[TF_SEPC] -= 4;
                trapframe[TF_A0]   = orig_a0;
            }
        }
    }

    check_signals(trapframe);
}

/* ----------------------------------------------------------------
 * sys_kill
 * ---------------------------------------------------------------- */
int64_t sys_kill(int pid, int sig) {
    if (sig < 0 || sig >= NSIG) return -EINVAL;

    if (pid > 0) {
        for (struct pcb *p = proc_list_head(); p; p = p->next) {
            if (p->pid == pid) {
                if (sig != 0) send_signal(p, sig);
                return 0;
            }
        }
        return -ESRCH;
    }

    int pgid;
    if (pid == 0) {
        struct pcb *me = current_proc();
        if (!me) return -EINVAL;
        pgid = me->pgid;
    } else if (pid == -1) {
        /* POSIX broadcast: send to every process the caller may signal.
         * SBUnix is single-user (uid==0 everywhere), so "may signal"
         * means "every non-init non-self user process". Kernel threads
         * (is_user == 0) are excluded because send_signal is a no-op for
         * them — counting them in `delivered` would let kill(-1, sig)
         * spuriously report success on a system that contains only kernel
         * threads and the caller. Init (pid 1) is excluded so cleanup
         * loops can't accidentally kill the system. Returns 0 if at
         * least one signal was actually delivered, -ESRCH if none.
         * sig == 0 falls through as an existence probe. */
        struct pcb *me = current_proc();
        int delivered = 0;
        for (struct pcb *p = proc_list_head(); p; p = p->next) {
            if (p->state == PROC_UNUSED || p->state == PROC_ZOMBIE) continue;
            if (!p->is_user) continue;
            if (p == me) continue;
            if (p->pid == 1) continue;
            if (sig != 0) send_signal(p, sig);
            delivered++;
        }
        return delivered > 0 ? 0 : -ESRCH;
    } else {
        pgid = -pid;
    }

    if (sig == 0) {
        /* Existence probe: return 0 if any member exists. */
        for (struct pcb *p = proc_list_head(); p; p = p->next)
            if (p->state != PROC_UNUSED && p->pgid == pgid) return 0;
        return -ESRCH;
    }

    int hits = send_signal_pgrp(pgid, sig);
    return hits ? 0 : -ESRCH;
}

/* ----------------------------------------------------------------
 * sys_sigaction
 * ---------------------------------------------------------------- */
int64_t sys_sigaction(int sig, const struct sigaction *act,
                      struct sigaction *oldact) {
    if (sig <= 0 || sig >= NSIG)         return -EINVAL;
    if (sig == SIGKILL || sig == SIGSTOP) return -EINVAL;

    struct pcb *p = current_proc();
    if (!p) return -EINVAL;

    /* Stage both sides before mutating state so a faulting user pointer
     * cannot leave the handler half-updated (and so oldact == act works). */
    struct sigaction kold = p->sig_handlers[sig];
    struct sigaction kact;
    if (act && copyin(&kact, act, sizeof(kact)) < 0)
        return -EFAULT;
    if (oldact && copyout(oldact, &kold, sizeof(kold)) < 0)
        return -EFAULT;
    if (act) {
        kact.sa_mask &= ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
        p->sig_handlers[sig] = kact;
    }
    return 0;
}

/* ----------------------------------------------------------------
 * sys_sigprocmask
 * ---------------------------------------------------------------- */
int64_t sys_sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    struct pcb *p = current_proc();
    if (!p) return -EINVAL;

    if (set && how != SIG_BLOCK && how != SIG_UNBLOCK && how != SIG_SETMASK)
        return -EINVAL;

    /* Stage both sides before mutating — atomic on partial user-copy failure. */
    sigset_t kold = p->sig_blocked;
    sigset_t s = 0;
    if (set && copyin(&s, set, sizeof(sigset_t)) < 0)
        return -EFAULT;
    if (oldset && copyout(oldset, &kold, sizeof(sigset_t)) < 0)
        return -EFAULT;
    if (set) {
        s &= ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
        switch (how) {
        case SIG_BLOCK:   p->sig_blocked |= s;  break;
        case SIG_UNBLOCK: p->sig_blocked &= ~s; break;
        case SIG_SETMASK: p->sig_blocked  = s;  break;
        default: return -EINVAL;
        }
    }
    return 0;
}

/* ----------------------------------------------------------------
 * sys_sigpending — read the set of pending signals.
 *
 * POSIX: returns the set of signals that are pending for the calling
 * thread (here: process — we don't have threads). Returning only the
 * currently deliverable/unblocked subset of p->sig_pending would not
 * match POSIX; POSIX requires the raw pending set regardless of
 * blocked state. We therefore return p->sig_pending as-is.
 * ---------------------------------------------------------------- */
int64_t sys_sigpending(sigset_t *set) {
    struct pcb *p = current_proc();
    if (!p) return -EINVAL;
    if (!set) return -EFAULT;
    sigset_t snap = p->sig_pending;
    if (copyout(set, &snap, sizeof(sigset_t)) < 0) return -EFAULT;
    return 0;
}

/* ----------------------------------------------------------------
 * sys_sigreturn — restore interrupted context from sigframe.
 * ---------------------------------------------------------------- */
int64_t sys_sigreturn(uint64_t *trapframe) {
    struct pcb *p = current_proc();
    if (!p) return -1;

    uint64_t frame_va = trapframe[1];   /* user sp after handler */

    struct sigframe fr;
    if (copyin(&fr, (const void *)frame_va, sizeof(fr)) < 0)
        proc_exit_current(SIGSEGV & 0x7f);

    if (fr.magic != SIGFRAME_MAGIC)
        proc_exit_current(SIGSEGV & 0x7f);

    /* Sanitize sstatus: force SPP=0, SPIE=1. */
    uint64_t safe_sstatus = fr.saved_trapframe[TF_SSTATUS];
    safe_sstatus &= ~(uint64_t)SSTATUS_SPP;
    safe_sstatus |=  (uint64_t)SSTATUS_SPIE;
    fr.saved_trapframe[TF_SSTATUS] = safe_sstatus;

    /* Sanitize sepc: must be in user VA range. */
    if (fr.saved_trapframe[TF_SEPC] >= KVMEM_OFFSET)
        proc_exit_current(SIGSEGV & 0x7f);

    memcpy(trapframe, fr.saved_trapframe, 288);

    p->sig_blocked = fr.saved_mask;
    p->sig_blocked &= ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
    p->in_sighandler = 0;

    return (int64_t)trapframe[TF_A0];
}

/* ----------------------------------------------------------------
 * sys_pause — sleep until any unblocked signal arrives.
 * ---------------------------------------------------------------- */
int64_t sys_pause(void) {
    struct pcb *p = current_proc();
    if (!p) return -EINVAL;
    while (!sig_has_actionable(p)) {
        proc_sleep(p);
    }
    return -EINTR;
}

/* ----------------------------------------------------------------
 * sys_sigsuspend — install arg mask, sleep until actionable signal,
 * arrange for the prior mask to be restored after the handler runs.
 *
 * POSIX requires the caller's mask to be restored before sigsuspend
 * returns to user code. We can't simply restore here because handler
 * delivery happens in check_signals on trap-return; restoring before
 * that would re-block the very signal that woke us. Instead we stash
 * the caller's mask in p->sig_suspend_saved_mask and set
 * sig_suspend_active. build_sigframe_and_redirect uses that saved
 * mask in fr.saved_mask (so sigreturn restores it). check_signals
 * also clears the flag and restores the saved mask if no handler was
 * actually delivered (default-action paths).
 * ---------------------------------------------------------------- */
int64_t sys_sigsuspend(const sigset_t *mask) {
    struct pcb *p = current_proc();
    if (!p || !mask) return -EINVAL;
    sigset_t s;
    if (copyin(&s, mask, sizeof(s)) < 0) return -EFAULT;
    s &= ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
    p->sig_suspend_saved_mask = p->sig_blocked;
    p->sig_suspend_active     = 1;
    p->sig_blocked            = s;
    while (!sig_has_actionable(p)) {
        proc_sleep(p);
    }
    return -EINTR;
}
