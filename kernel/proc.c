#include <proc.h>
#include <pmem.h>
#include <printk.h>
#include <riscv.h>
#include <string.h>
#include <vmem.h>

void forkret(void);   // forward declaration (defined below)

static struct pcb    *procs   = 0;   // head of all-processes list
static struct pcb    *current = 0;   // currently running process
static int            next_pid = 1;
static struct context sched_context;

struct pcb *current_proc(void) { return current; }

// ----------------------------------------------------------------
// alloc_proc — allocate a PCB + kernel stack from physical memory
// ----------------------------------------------------------------

static struct pcb *alloc_proc(void) {
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

static void free_proc(struct pcb *victim) {
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
        // Set up sstatus for a clean return to U-mode via sret:
        //   SPP = 0  → return to user mode
        //   SPIE = 1 → enable interrupts in user mode after sret
        uint64_t st = read_sstatus();
        st &= ~SSTATUS_SPP;
        st |=  SSTATUS_SPIE;
        write_sstatus(st);

        // write_sscratch with kernel stack top so trap.S can find it
        write_sscratch((uint64_t)p->kstack_page + KSTACK_SIZE);

        // write_sepc with user entry point, then sret via enter_user
        // (enter_user is added in Phase B; placeholder panic for now)
        panic("forkret: user mode entry not yet implemented");
    }

    // Kernel thread: just enable interrupts and call the entry function.
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

        swtch(&sched_context, &found->context);

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
    struct pcb *a = alloc_proc();
    if (!a) panic("sched_init: alloc_proc failed");
    a->state = PROC_READY;
    a->entry = thread_a;

    struct pcb *b = alloc_proc();
    if (!b) panic("sched_init: alloc_proc failed");
    b->state = PROC_READY;
    b->entry = thread_b;

    printk("scheduler: starting\n");
    scheduler_run();
}
