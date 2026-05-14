#include <exec.h>
#include <errno.h>
#include <file.h>
#include <inode.h>
#include <pmem.h>
#include <printk.h>
#include <proc.h>
#include <riscv.h>
#include <signal.h>
#include <string.h>
#include <syscall.h>
#include <termios.h>
#include <timer.h>
#include <resource.h>
#include <vmem.h>

// Declared in devfs.c — returns the /dev/console inode.
struct inode *devfs_console_inode(void);

void forkret(void);  // forward declaration (defined below)

// Assembly in user_enter.S — drops to U-mode and never returns
void enter_user(unsigned long satp, unsigned long user_entry,
                unsigned long user_sp, unsigned long kernel_sp);

// Assembly in user_enter.S — used by fork() to resume child in user mode
void fork_child_return(void);

static struct pcb    *procs   = 0;   // head of all-processes list
static struct pcb    *current = 0;   // currently running process
static int            next_pid = 1;
static int            init_pid = 0;  // pid of the user init, set in sched_init
static struct context sched_context;

struct pcb *current_proc(void)   { return current; }
struct pcb *proc_list_head(void) { return procs;   }

struct pcb *proc_find_by_pid(int pid) {
    uint64_t sstatus = read_sstatus();
    struct pcb *found = 0;

    write_sstatus(sstatus & ~SSTATUS_SIE);
    for (struct pcb *p = proc_list_head(); p; p = p->next) {
        if (p->pid == pid && p->state != PROC_UNUSED) {
            found = p;
            break;
        }
    }
    write_sstatus(read_sstatus() | (sstatus & SSTATUS_SIE));

    return found;
}

static void proc_unlink(struct pcb *victim) {
    struct pcb *prev = 0;
    int found = 0;
    for (struct pcb *p = procs; p; p = p->next) {
        if (p == victim) {
            found = 1;
            break;
        }
        prev = p;
    }
    if (!found)
        panic("proc_unlink: victim not found");
    if (prev) prev->next = victim->next;
    else if (procs == victim) procs = victim->next;
    victim->next = 0;
}

static void proc_destroy(struct pcb *p) {
    if (!p) return;
    if (current == p)
        panic("proc_destroy: destroying current process");

    // 1) remove from scheduler / process list.
    uint64_t sstatus = read_sstatus();
    write_sstatus(sstatus & ~SSTATUS_SIE);
    proc_unlink(p);
    write_sstatus(read_sstatus() | (sstatus & SSTATUS_SIE));

    // 2) close all open fds.
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            fileclose(p->ofile[fd]);
            p->ofile[fd] = 0;
        }
    }
    if (p->cwd) {
        inode_put(p->cwd);
        p->cwd = 0;
    }

    // 3) free VMA metadata.
    for (struct vma *vv = p->vma_list; vv; vv = vv->next) {
        if (vv->type == VMA_TYPE_FILE)
            vma_drop_file_pages(p, vv);
    }
    vma_list_free(&p->vma_list);
    p->heap_vma = 0;

    // 4) free user page table/pages.
    if (p->pagetable) {
        free_user_pgtable(p->pagetable);
        p->pagetable = 0;
    }

    // 5) free kernel stack.
    if (p->kstack_page) {
        page_free(p->kstack_page);
        p->kstack_page = 0;
    }

    // 6) free PCB page.
    page_free(p);
}

void proc_set_comm_basename(struct pcb *p, const char *src) {
    if (!p) return;
    /* Zero-fill so the trailing bytes after the terminator can't leak
     * stale data from a prior longer comm. */
    for (int i = 0; i < (int)sizeof(p->comm); i++) p->comm[i] = '\0';
    if (!src || !src[0]) return;
    int last_sep = -1;
    for (int i = 0; src[i]; i++) if (src[i] == '/') last_sep = i;
    int s = last_sep + 1;
    for (int j = 0; src[s + j] && j < (int)sizeof(p->comm) - 1; j++) {
        unsigned char c = (unsigned char)src[s + j];
        /* argv[0] is user-controlled and surfaces verbatim in
         * /proc/<pid>/stat (where comm is wrapped in parens) and
         * /proc/<pid>/status (line-oriented). Replace any byte that
         * would break those parsers with '_'. */
        if (c < 0x20 || c == 0x7f || c == '(' || c == ')')
            c = '_';
        p->comm[j] = (char)c;
    }
}

// ----------------------------------------------------------------
// alloc_proc — allocate a PCB + kernel stack from physical memory
// ----------------------------------------------------------------

struct pcb *alloc_proc(void) {
    struct pcb *p = (struct pcb *)page_alloc();
    if (p == 0) return 0;

    p->kstack_page = page_alloc();
    if (p->kstack_page == 0) {
        page_free(p);
        return 0;
    }

    p->pid        = next_pid++;
    static uint64_t generation_seq = 0;
    p->generation = ++generation_seq;
    p->parent_pid = 0;
    p->exit_status= 0;
    p->state      = PROC_UNUSED;
    p->pgid       = p->pid;
    p->sid        = p->pid;
    p->last_signal = 0;
    p->stopped_reported = 0;
    p->continued_pending = 0;
    p->did_exec   = 0;
    p->is_user    = 0;
    for (int i = 0; i < (int)sizeof(p->comm); i++) p->comm[i] = '\0';
    for (int i = 0; i < (int)sizeof(p->exe_path); i++) p->exe_path[i] = '\0';
    p->pagetable  = 0;
    p->user_entry = 0;
    p->user_sp    = 0;
    p->entry      = 0;
    p->sleep_chan = 0;
    p->wake_tick  = 0;
    p->alarm_tick = 0;
    p->vma_list   = 0;
    p->heap_vma   = 0;
    p->brk_start  = 0;
    p->next       = 0;

    p->sig_pending    = 0;
    p->sig_blocked    = 0;
    p->sig_saved_mask = 0;
    p->in_sighandler  = 0;
    p->delivering_segv= 0;
    p->sig_altstack.ss_sp    = 0;
    p->sig_altstack.ss_flags = SS_DISABLE;
    p->sig_altstack.ss_size  = 0;
    for (int i = 0; i < NSIG; i++) {
        p->sig_handlers[i].sa_handler  = SIG_DFL;
        p->sig_handlers[i].sa_mask     = 0;
        p->sig_handlers[i].sa_flags    = 0;
        p->sig_handlers[i].sa_restorer = 0;
    }

    for (int i = 0; i < RLIMITS_NR; i++) {
        p->rlim[i].rlim_cur = RLIM_INFINITY;
        p->rlim[i].rlim_max = RLIM_INFINITY;
    }
    p->rlim[RLIMIT_STACK].rlim_cur  = DEFAULT_STACK_SOFT;
    p->rlim[RLIMIT_STACK].rlim_max  = DEFAULT_STACK_HARD;
    p->rlim[RLIMIT_NOFILE].rlim_cur = NOFILE;
    p->rlim[RLIMIT_NOFILE].rlim_max = NOFILE;
    // context is zeroed by page_alloc; set sp and ra
    p->context.sp = (uint64_t)p->kstack_page + KSTACK_SIZE;
    p->context.ra = (uint64_t)forkret;

    // append to process list
    uint64_t sstatus = read_sstatus();
    write_sstatus(sstatus & ~SSTATUS_SIE);
    if (procs == 0) {
        procs = p;
    } else {
        struct pcb *tail = procs;
        while (tail->next) tail = tail->next;
        tail->next = p;
    }
    write_sstatus(read_sstatus() | (sstatus & SSTATUS_SIE));

    return p;
}

// ----------------------------------------------------------------
// free_proc — release PCB + kernel stack of a ZOMBIE process
// ----------------------------------------------------------------

void free_proc(struct pcb *victim) {
    proc_destroy(victim);
}

// ----------------------------------------------------------------
// forkret — first-run entry point for every new process
// ----------------------------------------------------------------

void forkret(void) {
    struct pcb *p = current;

    if (p->is_user) {
        uint64_t ksp = (uint64_t)p->kstack_page + KSTACK_SIZE;
        printk("[forkret] pid=%d: entering user mode, entry=0x%lx sp=0x%lx ksp=0x%lx satp=0x%lx\n",
               p->pid, p->user_entry, p->user_sp, ksp, make_satp(p->pagetable));
        // enter_user switches to the user page table, sets sscratch, sepc,
        // sstatus (SPP=0, SPIE=1), user sp, then sret.  Never returns.
        enter_user(make_satp(p->pagetable), p->user_entry, p->user_sp, ksp);
        // unreachable
    }

    // Kernel thread: enable interrupts and call the entry function.
    write_sstatus(read_sstatus() | SSTATUS_SIE);
    p->entry();

    // Entry function returned — mark zombie and yield forever.
    p->state = PROC_ZOMBIE;
    swtch(&p->context, &sched_context);
    panic("zombie process resumed");
}

// ----------------------------------------------------------------
// proc_fork_current — duplicate the current user process
// ----------------------------------------------------------------

int proc_fork_current(void) {
    struct pcb *parent = current;
    if (!parent || !parent->is_user) return -1;

    /* Enforce RLIMIT_NPROC (number of processes for the same uid). SBUnix
     * is single-user, so we count every non-unused, non-zombie process
     * other than init (pid 1). */
    rlim_t nproc_max = parent->rlim[RLIMIT_NPROC].rlim_cur;
    if (nproc_max != RLIM_INFINITY) {
        rlim_t live = 0;
        for (struct pcb *q = proc_list_head(); q; q = q->next) {
            if (q->state == PROC_UNUSED || q->state == PROC_ZOMBIE) continue;
            if (q->pid == 1) continue;
            live++;
        }
        if (live >= nproc_max) return -EAGAIN;
    }

    struct pcb *child = alloc_proc();
    if (!child) return -ENOMEM;

    pgtable_t child_pt = uvmcow_share(parent->pagetable);
    if (!child_pt) {
        proc_destroy(child);
        return -ENOMEM;
    }

    // The trap frame is always at kstack_top - 288 (trap.S: addi sp, sp, -288
    // from kstack_top for every U-mode trap).
    uint64_t  parent_kstop = (uint64_t)parent->kstack_page + KSTACK_SIZE;
    uint64_t  child_kstop  = (uint64_t)child->kstack_page  + KSTACK_SIZE;
    uint64_t *parent_tf    = (uint64_t *)(parent_kstop - 288);
    uint64_t *child_tf     = (uint64_t *)(child_kstop  - 288);

    // Copy the complete trap frame (288 bytes = 36 × uint64_t)
    memmove(child_tf, parent_tf, 288);

    // Sanity: parent trapframe must be returning to user. Catch corruption
    // at the source rather than letting the child sret to a kernel PC.
    if (parent_tf[TF_SEPC] >= USER_STACK_TOP || parent_tf[1] == 0 ||
        parent_tf[1] >= USER_STACK_TOP) {
        printk("[BUG fork] parent pid=%d has bad tf: sepc=0x%lx sp=0x%lx\n",
               parent->pid, parent_tf[TF_SEPC], parent_tf[1]);
    }

    // fork() returns 0 in the child
    child_tf[TF_A0] = 0;

    // Set up child context: swtch() will load ra and sp, call ret →
    // fork_child_return restores the trap frame and srets to user mode.
    child->context.ra = (uint64_t)fork_child_return;
    child->context.sp = (uint64_t)child_tf;

    // Duplicate open file descriptors into the child.
    for (int fd = 0; fd < NOFILE; fd++) {
        if (parent->ofile[fd])
            child->ofile[fd] = filedup(parent->ofile[fd]);
    }
    if (parent->cwd) {
        child->cwd = inode_get(parent->cwd);
        // Copy cwd path string.
        int i = 0;
        while (parent->cwd_path[i] && i < 255) {
            child->cwd_path[i] = parent->cwd_path[i];
            i++;
        }
        child->cwd_path[i] = '\0';
    }

    child->is_user    = 1;
    child->pagetable  = child_pt;
    child->user_entry = parent->user_entry;
    child->user_sp    = parent->user_sp;
    child->parent_pid = parent->pid;
    child->brk_start  = parent->brk_start;

    /* Inherit pgid/sid; pid-derived defaults from alloc_proc are overwritten. */
    child->pgid       = parent->pgid;
    child->sid        = parent->sid;
    for (int i = 0; i < (int)sizeof(child->comm); i++)
        child->comm[i] = parent->comm[i];
    for (int i = 0; i < (int)sizeof(child->exe_path); i++)
        child->exe_path[i] = parent->exe_path[i];
    child->last_signal = 0;
    child->stopped_reported = 0;
    child->continued_pending = 0;

    /* Inherit signal handlers and mask; child starts with no pending signals. */
    for (int i = 0; i < NSIG; i++)
        child->sig_handlers[i] = parent->sig_handlers[i];
    child->sig_blocked    = parent->sig_blocked;
    child->sig_pending    = 0;
    child->in_sighandler  = 0;
    child->delivering_segv= 0;
    /* POSIX: sigaltstack settings inherited across fork. The child is not
     * itself currently in a signal handler, but if the parent forked from
     * inside one running on the alt stack, the child must remember that
     * (so its sigaltstack(ss, NULL) returns EPERM until it sigreturns). */
    child->sig_altstack    = parent->sig_altstack;
    for (int i = 0; i < RLIMITS_NR; i++)
        child->rlim[i] = parent->rlim[i];

    child->vma_list = vma_list_dup(parent->vma_list);
    if (!child->vma_list) {
        proc_destroy(child);
        return -ENOMEM;
    }
    /* Bump pcache refs for file-backed PTEs inherited via uvmcow_share so
     * the child holds its own fault-time refs (uvmcow_share only bumps
     * anon page_ref, not pcache slot refcnts). */
    for (struct vma *v = child->vma_list; v; v = v->next) {
        if (v->type == VMA_TYPE_FILE && vma_dup_file_pages(child, v) < 0) {
            proc_destroy(child);
            return -ENOMEM;
        }
    }
    child->heap_vma = 0;
    for (struct vma *v = child->vma_list; v; v = v->next) {
        if (v->type == VMA_TYPE_HEAP) {
            child->heap_vma = v;
            break;
        }
    }

    for (struct vma *v = parent->vma_list; v; v = v->next)
        if (v->prot & VMA_PROT_W) v->flags |= VMA_FLAG_COW;
    for (struct vma *v = child->vma_list; v; v = v->next)
        if (v->prot & VMA_PROT_W) v->flags |= VMA_FLAG_COW;

    child->state      = PROC_READY;

    return child->pid;
}

// ----------------------------------------------------------------
// proc_stop_current — current proc has been marked PROC_STOPPED by
// signal delivery; relinquish the CPU. Resumes when SIGCONT switches
// state back to PROC_READY and scheduler picks us again.
// ----------------------------------------------------------------

void proc_stop_current(void) {
    if (!current) return;
    /* state already set to PROC_STOPPED by caller */
    swtch(&current->context, &sched_context);
}

// ----------------------------------------------------------------
// yield — give up CPU, switch back to scheduler
// ----------------------------------------------------------------

void yield(void) {
    if (current == 0 || current->state != PROC_RUNNING)
        return;

    struct pcb *p = current;

    uint64_t sstatus = read_sstatus();
    write_sstatus(sstatus & ~SSTATUS_SIE);   // disable interrupts

    p->state = PROC_READY;
    swtch(&p->context, &sched_context);

    // Resumed — restore previous interrupt state.
    write_sstatus(read_sstatus() | (sstatus & SSTATUS_SIE));
}

// ----------------------------------------------------------------
// sleep / wakeup — basic blocking mechanism
// ----------------------------------------------------------------

void proc_sleep(struct pcb *p) {
    p->state = PROC_SLEEPING;
    swtch(&p->context, &sched_context);
}

void proc_wakeup(int pid) {
    for (struct pcb *p = procs; p; p = p->next) {
        if (p->pid == pid && p->state == PROC_SLEEPING)
            p->state = PROC_READY;
    }
}

void proc_sleep_chan(void *chan) {
    struct pcb *p = current_proc();
    if (!p) return;
    p->sleep_chan = chan;
    proc_sleep(p);
    p->sleep_chan = 0;
}

void proc_wakeup_chan(void *chan) {
    for (struct pcb *p = procs; p; p = p->next) {
        if (p->state == PROC_SLEEPING && p->sleep_chan == chan)
            p->state = PROC_READY;
    }
}

// ----------------------------------------------------------------
// proc_exit_current — transition current process to ZOMBIE
// ----------------------------------------------------------------

void proc_exit_current(int status) {
    if (current == 0 || current->state != PROC_RUNNING)
        return;

    struct pcb *p = current;
    p->exit_status = status;

    // Safe unlocked on single-hart: only scheduler-context code mutates
    // the process list, and proc_exit_current runs in scheduler context.
    // Reparent to the actual init pid: selftests consume low pids before
    // init is spawned, so init is not guaranteed to be pid 1.
    int reparent_to = init_pid ? init_pid : 1;
    int reparented_any = 0;
    for (struct pcb *it = procs; it; it = it->next) {
        if (it->parent_pid == p->pid) {
            it->parent_pid = reparent_to;
            reparented_any = 1;
        }
    }
    if (reparented_any)
        proc_wakeup(reparent_to);

    // Close all open file descriptors so pipes/devices see EOF.
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            fileclose(p->ofile[fd]);
            p->ofile[fd] = 0;
        }
    }

    // Session-leader exit: if this proc is the session leader and the
    // session owns the controlling terminal, hang up every member of
    // every pgrp in the session (SIGHUP + SIGCONT to wake stopped jobs).
    if (p->pid == p->sid && termios_get_session() == p->sid) {
        for (struct pcb *q = procs; q; q = q->next) {
            if (q == p || q->state == PROC_UNUSED) continue;
            if (q->sid != p->sid) continue;
            send_signal(q, SIGHUP);
            send_signal(q, SIGCONT);
        }
        termios_set_session(0);
        termios_set_fg_pgid(0);
    }

    // Notify parent: send SIGCHLD, then wake it if sleeping in wait
    send_signal_by_pid(p->parent_pid, SIGCHLD);
    proc_wakeup(p->parent_pid);

    p->state = PROC_ZOMBIE;
    swtch(&p->context, &sched_context);
    panic("exited process resumed");
}

// ----------------------------------------------------------------
// proc_wait_current — wait for any zombie child; returns child pid
// ----------------------------------------------------------------

int proc_wait_current(int *status) {
    return proc_wait4_current(-1, status, 0);
}

#define WNOHANG_K    1
#define WUNTRACED_K  2
#define WCONTINUED_K 8

static int wait4_match(struct pcb *child, struct pcb *parent, int pid) {
    if (pid > 0)  return child->pid  == pid;
    if (pid == 0) return child->pgid == parent->pgid;
    if (pid == -1) return 1;
    return child->pgid == -pid;
}

int proc_wait4_current(int pid, int *status, int options) {
    if (current == 0) return -ECHILD;

    while (1) {
        int found_child = 0;

        for (struct pcb *p = procs; p; p = p->next) {
            if (p->parent_pid != current->pid) continue;
            if (!wait4_match(p, current, pid)) continue;
            found_child = 1;

            if (p->state == PROC_ZOMBIE) {
                int cpid = p->pid;
                if (status) *status = p->exit_status;
                proc_destroy(p);
                return cpid;
            }
            if ((options & WUNTRACED_K) && p->state == PROC_STOPPED &&
                !p->stopped_reported) {
                p->stopped_reported = 1;
                if (status)
                    *status = ((p->last_signal & 0xff) << 8) | 0x7f;
                return p->pid;
            }
            if ((options & WCONTINUED_K) && p->continued_pending) {
                p->continued_pending = 0;
                if (status) *status = 0xffff;
                return p->pid;
            }
        }

        if (!found_child) return -ECHILD;
        if (options & WNOHANG_K) return 0;
        if (sig_has_actionable(current)) return -ERESTARTSYS;
        proc_sleep(current);
    }
}

// ----------------------------------------------------------------
// next_ready_proc — round-robin selection (skips current)
// ----------------------------------------------------------------

static struct pcb *next_ready_proc(void) {
    if (procs == 0) return 0;

    // Start search after current process
    struct pcb *start = current ? current->next : procs;
    if (start == 0) start = procs;

    struct pcb *p = start;
    do {
        if (p->state == PROC_READY) return p;
        p = p->next ? p->next : procs;
    } while (p != start);

    return 0;
}

// ----------------------------------------------------------------
// scheduler loop — runs with interrupts disabled
// ----------------------------------------------------------------

static void scheduler_run(void) {
    while (1) {
        // Restore kernel page table before scanning
        write_satp(make_satp(kernel_pgtable));
        flush_tlb();

        struct pcb *found = next_ready_proc();

        if (found == 0) {
            // No runnable process — enable interrupts briefly so a timer
            // tick or wakeup can fire, then retry.
            write_sstatus(read_sstatus() | SSTATUS_SIE);
            asm volatile("wfi");
            write_sstatus(read_sstatus() & ~SSTATUS_SIE);
            continue;
        }

        current = found;
        found->state = PROC_RUNNING;

        // For user processes, set sscratch to kernel stack top so trap.S
        // can switch stacks on the next trap from U-mode.
        if (found->is_user)
            write_sscratch((uint64_t)found->kstack_page + KSTACK_SIZE);
        else
            write_sscratch(0);

        // Switch to the process's page table before entering its context.
        // For user processes this loads the user SATP (which still has the
        // kernel upper-half mapped), for kernel threads we stay on kernel SATP.
        if (found->is_user && found->pagetable) {
            write_satp(make_satp(found->pagetable));
            flush_tlb();
        }

        swtch(&sched_context, &found->context);
        // After swtch returns (process yielded/blocked/exited), the top of
        // the loop restores the kernel SATP.

        // Returned from process — reap parentless zombies immediately.
        // Switch to the kernel page table first: the hart may still be
        // translating through the zombie's user pagetable, and proc_destroy
        // calls free_user_pgtable which frees those pages.
        if (current && current->state == PROC_ZOMBIE && current->parent_pid == 0) {
            struct pcb *z = current;
            current = 0;
            write_satp(make_satp(kernel_pgtable));
            flush_tlb();
            proc_destroy(z);
        }
    }
}

// ----------------------------------------------------------------
// proc_sleep_ms — sleep for (at least) ms milliseconds
// ----------------------------------------------------------------

void proc_sleep_ms(uint64_t ms) {
    struct pcb *p = current;
    if (!p) return;
    if (ms == 0) { yield(); return; }

    uint64_t wake = timer_ticks() + ms_to_ticks(ms);
    if (wake == 0) wake = 1;  // never use 0 as a deadline
    p->wake_tick = wake;
    proc_sleep(p);             // marks SLEEPING, swtches to scheduler
    // Resumed by timer_handler once wake_tick <= ticks.
    p->wake_tick = 0;
}

// ----------------------------------------------------------------
// sched_init — called once from boot(), never returns
// ----------------------------------------------------------------

void sched_init(void) {
    struct pcb *init = proc_spawn("/bin/init");
    if (!init) panic("sched_init: failed to spawn init");
    init_pid = init->pid;

    printk("scheduler: starting\n");
    scheduler_run();
}
