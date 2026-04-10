#include <exec.h>
#include <pmem.h>
#include <printk.h>
#include <proc.h>
#include <riscv.h>
#include <string.h>
#include <syscall.h>
#include <vmem.h>

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

struct pcb *current_proc(void) { return current; }

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
    p->next       = 0;

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
// Test kernel threads (Phase A verification)
// ----------------------------------------------------------------

static void thread_a(void) {
    int i = 0;
    while (1) {
        printk("[A] tick %d\n", i++);
        yield();
    }
}

static void thread_b(void) {
    int i = 0;
    while (1) {
        printk("[B] tick %d\n", i++);
        yield();
    }
}

// ----------------------------------------------------------------
// proc_fork_current — duplicate the current user process
// ----------------------------------------------------------------

int proc_fork_current(void) {
    struct pcb *parent = current;
    if (!parent || !parent->is_user) return -1;

    struct pcb *child = alloc_proc();
    if (!child) return -1;

    // Deep-copy user address space
    pgtable_t child_pt = uvmcopy(parent->pagetable);
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

    child->is_user    = 1;
    child->pagetable  = child_pt;
    child->user_entry = parent->user_entry;
    child->user_sp    = parent->user_sp;
    child->parent_pid = parent->pid;
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

// ----------------------------------------------------------------
// proc_exit_current — transition current process to ZOMBIE
// ----------------------------------------------------------------

void proc_exit_current(int status) {
    if (current == 0 || current->state != PROC_RUNNING)
        return;

    struct pcb *p = current;
    p->exit_status = status;

    // Reparent children to init (pid 1) so they get reaped
    for (struct pcb *it = procs; it; it = it->next) {
        if (it->parent_pid == p->pid)
            it->parent_pid = 1;
    }

    // Wake parent if it's sleeping in wait
    proc_wakeup(p->parent_pid);

    p->state = PROC_ZOMBIE;
    swtch(&p->context, &sched_context);
    panic("exited process resumed");
}

// ----------------------------------------------------------------
// proc_wait_current — wait for any zombie child; returns child pid
// ----------------------------------------------------------------

int proc_wait_current(int *status) {
    if (current == 0) return -1;

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

        if (!found_child) return -1;   // no children at all

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
        if (current && current->state == PROC_ZOMBIE && current->parent_pid == 0) {
            struct pcb *z = current;
            current = 0;
            free_proc(z);
        }
    }
}

// ----------------------------------------------------------------
// sched_init — called once from boot(), never returns
// ----------------------------------------------------------------

void sched_init(void) {
    // --- kernel test threads ---
    struct pcb *a = alloc_proc();
    if (!a) panic("sched_init: alloc_proc failed");
    a->state = PROC_READY;
    a->entry = thread_a;

    struct pcb *b = alloc_proc();
    if (!b) panic("sched_init: alloc_proc failed");
    b->state = PROC_READY;
    b->entry = thread_b;

    // --- Phase D: fork test ---
    struct pcb *ft = proc_spawn("bin/fork_test");
    if (!ft) panic("sched_init: failed to spawn /bin/fork_test");

    printk("scheduler: starting\n");
    scheduler_run();
}
