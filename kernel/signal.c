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
    [SIGCHLD] = ACT_IGN,  [SIGCONT] = ACT_IGN,   [SIGSTOP] = ACT_TERM,
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

    target->sig_pending |= (1ULL << sig);

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

int sig_has_actionable(struct pcb *p) {
    if (!p) return 0;
    uint64_t deliverable = p->sig_pending & ~p->sig_blocked;
    if (!deliverable) return 0;

    for (int i = 1; i < NSIG; i++) {
        if (!(deliverable & (1ULL << i))) continue;
        sighandler_t h = p->sig_handlers[i].sa_handler;
        if (h == SIG_IGN) continue;
        if (h == SIG_DFL && default_action[i] == ACT_IGN) continue;
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
        proc_exit_current(128 + SIGSEGV);
    }

    /* Build the frame in kernel memory, then copyout. */
    struct sigframe fr;
    memset(&fr, 0, sizeof(fr));
    fr.magic      = SIGFRAME_MAGIC;
    fr.saved_mask = p->sig_blocked;
    memcpy(fr.saved_trapframe, tf, 288);

    /* Write frame to user stack — faults in lazy/COW pages as needed. */
    if (copyout((void *)frame_va, &fr, sizeof(fr)) < 0) {
        p->delivering_segv = 1;
        proc_exit_current(128 + SIGSEGV);
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

    uint64_t deliverable = p->sig_pending & ~p->sig_blocked;
    if (deliverable == 0) return;

    /* Pick lowest-numbered deliverable signal. */
    int sig = 0;
    for (int i = 1; i < NSIG; i++) {
        if (deliverable & (1ULL << i)) { sig = i; break; }
    }
    if (sig == 0) return;

    p->sig_pending &= ~(1ULL << sig);

    /* SIGKILL/SIGSTOP: always kill. */
    if (sig == SIGKILL || sig == SIGSTOP) {
        proc_exit_current(128 + sig);
    }

    sighandler_t h = p->sig_handlers[sig].sa_handler;

    if (h == SIG_IGN) return;

    if (h == SIG_DFL) {
        uint8_t act = (sig < NSIG) ? default_action[sig] : ACT_TERM;
        if (act == ACT_IGN) return;
        if (sig == SIGSEGV && p->delivering_segv) {
            /* Recursive SIGSEGV — just die. */
        }
        proc_exit_current(128 + sig);
    }

    /* Custom handler — redirect return-to-user into handler. */
    build_sigframe_and_redirect(p, trapframe, sig, h);
}

/* ----------------------------------------------------------------
 * sys_kill
 * ---------------------------------------------------------------- */
int64_t sys_kill(int pid, int sig) {
    if (sig < 0 || sig >= NSIG) return -EINVAL;
    if (pid <= 0) return -EINVAL;

    for (struct pcb *p = proc_list_head(); p; p = p->next) {
        if (p->pid == pid) {
            if (sig != 0) send_signal(p, sig);
            return 0;
        }
    }
    return -ESRCH;
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
        }
    }
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
        proc_exit_current(128 + SIGSEGV);

    if (fr.magic != SIGFRAME_MAGIC)
        proc_exit_current(128 + SIGSEGV);

    /* Sanitize sstatus: force SPP=0, SPIE=1. */
    uint64_t safe_sstatus = fr.saved_trapframe[TF_SSTATUS];
    safe_sstatus &= ~(uint64_t)SSTATUS_SPP;
    safe_sstatus |=  (uint64_t)SSTATUS_SPIE;
    fr.saved_trapframe[TF_SSTATUS] = safe_sstatus;

    /* Sanitize sepc: must be in user VA range. */
    if (fr.saved_trapframe[TF_SEPC] >= KVMEM_OFFSET)
        proc_exit_current(128 + SIGSEGV);

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
