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
#include <timer.h>
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
static struct context sched_context;

struct pcb *current_proc(void)   { return current; }
struct pcb *proc_list_head(void) { return procs;   }

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
    p->parent_pid = 0;
    p->exit_status= 0;
    p->state      = PROC_UNUSED;
    p->is_user    = 0;
    p->pagetable  = 0;
    p->user_entry = 0;
    p->user_sp    = 0;
    p->entry      = 0;
    p->sleep_chan = 0;
    p->vma_list   = 0;
    p->heap_vma   = 0;
    p->brk_start  = 0;
    p->next       = 0;

    p->sig_pending    = 0;
    p->sig_blocked    = 0;
    p->sig_saved_mask = 0;
    p->in_sighandler  = 0;
    p->delivering_segv= 0;
    for (int i = 0; i < NSIG; i++) {
        p->sig_handlers[i].sa_handler  = SIG_DFL;
        p->sig_handlers[i].sa_mask     = 0;
        p->sig_handlers[i].sa_flags    = 0;
        p->sig_handlers[i].sa_restorer = 0;
    }

    // context is zeroed by page_alloc; set sp and ra
    p->context.sp = (uint64_t)p->kstack_page + KSTACK_SIZE;
    p->context.ra = (uint64_t)forkret;

    // append to process list
    if (procs == 0) {
        procs = p;
    } else {
        struct pcb *tail = procs;
        while (tail->next) tail = tail->next;
        tail->next = p;
    }

    return p;
}

// ----------------------------------------------------------------
// free_proc — release PCB + kernel stack of a ZOMBIE process
// ----------------------------------------------------------------

void free_proc(struct pcb *victim) {
    // Unlink from list
    struct pcb *prev = 0;
    for (struct pcb *p = procs; p; p = p->next) {
        if (p == victim) break;
        prev = p;
    }
    if (prev) prev->next = victim->next;
    else      procs      = victim->next;

    // Close any open file descriptors and drop cwd reference.
    // Normally proc_exit_current does this, but free_proc is also called on
    // processes that never ran (e.g. test_leak_spawn_free), so we must handle it.
    for (int fd = 0; fd < NOFILE; fd++) {
        if (victim->ofile[fd]) {
            fileclose(victim->ofile[fd]);
            victim->ofile[fd] = 0;
        }
    }
    if (victim->cwd) {
        inode_put(victim->cwd);
        victim->cwd = 0;
    }

    // Free VMAs before page table (metadata only — actual pages freed below).
    vma_list_free(&victim->vma_list);
    victim->heap_vma = 0;

    // Free user page table BEFORE the kstack and PCB pages.
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

    struct pcb *child = alloc_proc();
    if (!child) return -1;

    pgtable_t child_pt = uvmcow_share(parent->pagetable);
    if (!child_pt) {
        free_proc(child);
        return -1;
    }

    // The trap frame is always at kstack_top - 288 (trap.S: addi sp, sp, -288
    // from kstack_top for every U-mode trap).
    uint64_t  parent_kstop = (uint64_t)parent->kstack_page + KSTACK_SIZE;
    uint64_t  child_kstop  = (uint64_t)child->kstack_page  + KSTACK_SIZE;
    uint64_t *parent_tf    = (uint64_t *)(parent_kstop - 288);
    uint64_t *child_tf     = (uint64_t *)(child_kstop  - 288);

    // Copy the complete trap frame (288 bytes = 36 × uint64_t)
    memmove(child_tf, parent_tf, 288);

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

    /* Inherit signal handlers and mask; child starts with no pending signals. */
    for (int i = 0; i < NSIG; i++)
        child->sig_handlers[i] = parent->sig_handlers[i];
    child->sig_blocked    = parent->sig_blocked;
    child->sig_pending    = 0;
    child->in_sighandler  = 0;
    child->delivering_segv= 0;

    child->vma_list = vma_list_dup(parent->vma_list);
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

    printk("[fork] parent pid=%d -> child pid=%d\n", parent->pid, child->pid);
    return child->pid;
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

    // Close all open file descriptors.
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            fileclose(p->ofile[fd]);
            p->ofile[fd] = 0;
        }
    }
    // Drop reference to current working directory.
    if (p->cwd) {
        inode_put(p->cwd);
        p->cwd = 0;
    }

    // Reparent children to init (pid 1) so they get reaped
    for (struct pcb *it = procs; it; it = it->next) {
        if (it->parent_pid == p->pid)
            it->parent_pid = 1;
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
    if (current == 0) return -ECHILD;

    while (1) {
        int found_child = 0;

        for (struct pcb *p = procs; p; p = p->next) {
            if (p->parent_pid != current->pid) continue;
            found_child = 1;
            if (p->state == PROC_ZOMBIE) {
                int cpid = p->pid;
                if (status) *status = p->exit_status;
                free_proc(p);
                return cpid;
            }
        }

        if (!found_child) return -ECHILD;   // no children at all

        // If interrupted by an actionable signal while no child is ready,
        // report EINTR. Reaping always takes priority when a zombie exists.
        // SIGCHLD is default-ignored, so it does not interrupt wait().
        if (sig_has_actionable(current))
            return -EINTR;

        // Sleep until a child exits
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
        // translating through the zombie's user pagetable, and free_proc
        // calls free_user_pgtable which frees those pages.
        if (current && current->state == PROC_ZOMBIE && current->parent_pid == 0) {
            struct pcb *z = current;
            current = 0;
            write_satp(make_satp(kernel_pgtable));
            flush_tlb();
            free_proc(z);
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
    struct pcb *init = proc_spawn("bin/init");
    if (!init) panic("sched_init: failed to spawn init");

    printk("scheduler: starting\n");
    scheduler_run();
}
