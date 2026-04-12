#include <proc.h>
#include <pmem.h>
#include <printk.h>
#include <riscv.h>
#include <string.h>
#include <tarfs.h>
#include <elf.h>

static struct pcb     *procs;
static struct pcb     *current;
static int             next_pid = 1;
static struct context  sched_context;

#define DEMO_TICK_LIMIT 10

extern void enter_user(unsigned long satp, unsigned long sepc, unsigned long usp, unsigned long ksp);

/* init_user_prog is loaded from tarfs at sched_init time */

// ----------------------------------------------------------------
// forkret — first-run entry point for new threads.
//
// When swtch first transfers control to a new thread, there is no
// prior sret to restore interrupts (SIE is cleared because we
// inherited the scheduler's interrupt-disabled state).  forkret
// enables interrupts and calls the thread's entry function.
// ----------------------------------------------------------------

static void forkret(void) {
    struct pcb *p = current;

    if (p->is_user) {
        uint64_t sstatus = read_sstatus();
        sstatus &= ~SSTATUS_SPP;   // return to U-mode
        sstatus |= SSTATUS_SPIE;   // enable interrupts in U-mode after sret
        write_sstatus(sstatus);
        enter_user(make_satp(p->pagetable), p->user_entry, p->user_sp,
             (unsigned long)p->kstack_page + KSTACK_SIZE);
        panic("enter_user returned unexpectedly");
    }

    write_sstatus(read_sstatus() | SSTATUS_SIE);
    p->entry();

    // If the thread function ever returns, mark it exited and
    // switch back to the scheduler permanently.
    p->state = PROC_ZOMBIE;
    swtch(&p->context, &sched_context);
    panic("zombie process resumed");
}

// ----------------------------------------------------------------
// Test threads
// ----------------------------------------------------------------

static void thread_a(void) {
    for (int i = 0; i < DEMO_TICK_LIMIT; i++) {
        printk("[A] tick %d\n", i);
        yield();
    }
}

static void thread_b(void) {
    for (int i = 0; i < DEMO_TICK_LIMIT; i++) {
        printk("[B] tick %d\n", i);
        yield();
    }
}

static void reap_proc(struct pcb *victim) {
    struct pcb *prev = 0;
    struct pcb *p = procs;

    while (p && p != victim) {
        prev = p;
        p = p->next;
    }

    if (p == 0) {
        return;
    }

    if (prev) {
        prev->next = victim->next;
    } else {
        procs = victim->next;
    }

    if (victim->kstack_page) {
        page_free(victim->kstack_page);
        victim->kstack_page = 0;
    }

    page_free(victim);
}

static struct pcb *alloc_proc(void) {
    struct pcb *p = (struct pcb *)page_alloc();
    if (p == 0) {
        return 0;
    }
    memset(p, 0, PAGE_SIZE);

    p->kstack_page = page_alloc();
    if (p->kstack_page == 0) {
        page_free(p);
        return 0;
    }

    p->pid = next_pid++;
    p->state = PROC_UNUSED;

    if (procs == 0) {
        procs = p;
    } else {
        struct pcb *tail = procs;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = p;
    }

    return p;
}

static void proc_init_kernel(void (*func)(void)) {
    struct pcb *p = alloc_proc();
    if (p == 0) {
        panic("Failed to allocate kernel process");
    }

    p->state = PROC_READY;
    p->is_user = 0;
    p->entry = func;

    memset(&p->context, 0, sizeof(struct context));
    p->context.sp = (uint64_t)p->kstack_page + KSTACK_SIZE;
    p->context.ra = (uint64_t)forkret;
}

static void proc_init_user(char *img, unsigned long size) {
    struct pcb *p = alloc_proc();
    if (p == 0) {
        panic("Failed to allocate user process");
    }

    p->state = PROC_READY;
    p->is_user = 1;
    p->entry = 0;

    p->pagetable = create_user_pgtable();
    p->user_entry = elf_load(p->pagetable, img, size);
    if (p->user_entry == 0) panic("elf_load failed");
    p->user_sp = map_stack(p->pagetable);

    if (p->user_sp == 0) {
        panic("Failed to map user stack");
    }

    memset(&p->context, 0, sizeof(struct context));
    p->context.sp = (uint64_t)p->kstack_page + KSTACK_SIZE;
    p->context.ra = (uint64_t)forkret;
}

static struct pcb *next_ready_proc(void) {
    if (procs == 0) {
        return 0;
    }

    struct pcb *start = current ? current->next : procs;
    if (start == 0) {
        start = procs;
    }

    struct pcb *p = start;
    do {
        if (p->state == PROC_READY) {
            return p;
        }
        p = p->next ? p->next : procs;
    } while (p != start);

    return 0;
}

// ----------------------------------------------------------------
// yield — give up CPU, switch back to scheduler
//
// Called from the timer interrupt handler (SIE already cleared by
// hardware) or voluntarily from a thread (SIE is enabled).  We
// save and restore the SIE bit so both paths work correctly:
//   - interrupt path: SIE stays cleared through scheduler, restored
//     by sret when we eventually return through trap_vector.
//   - voluntary path: SIE is cleared for the scheduler, then
//     re-enabled when this thread resumes.
// ----------------------------------------------------------------

void yield(void) {
    if (current == 0 || current->state != PROC_RUNNING)
        return;

    struct pcb *p = current;

    uint64_t sstatus = read_sstatus();
    write_sstatus(sstatus & ~SSTATUS_SIE);   // interrupts off

    p->state = PROC_READY;
    swtch(&p->context, &sched_context);

    // Resumed by the scheduler — restore previous interrupt state.
    write_sstatus(read_sstatus() | (sstatus & SSTATUS_SIE));
}

// ----------------------------------------------------------------
// scheduler loop — runs with interrupts disabled
// ----------------------------------------------------------------

static void scheduler_run(void) {
    while (1) {
        struct pcb *found = next_ready_proc();

        if (found == 0) {
            printk("scheduler: no runnable processes\n");
            while (1)
                asm volatile("wfi");
        }

        current = found;
        found->state = PROC_RUNNING;
        swtch(&sched_context, &found->context);

        if (current && current->state == PROC_ZOMBIE) {
            struct pcb *zombie = current;
            current = 0;
            reap_proc(zombie);
        }
    }
}

// ----------------------------------------------------------------
// sched_init — called once from boot(), never returns
// ----------------------------------------------------------------

extern void syscall_test_thread(void);

void sched_init(void) {
    procs = 0;
    current = 0;

    proc_init_kernel(thread_a);
    proc_init_kernel(thread_b);
    proc_init_kernel(syscall_test_thread);

    unsigned long ls_size = 0;
    char *ls_img = tarfs_find("bin/ls", &ls_size);
    if (ls_img && ls_size > 0) {
        printk("sched_init: loading bin/ls (%lu bytes) from tarfs\n", ls_size);
        proc_init_user(ls_img, ls_size);
    } else {
        printk("sched_init: bin/ls not found in tarfs\n");
    }

    printk("scheduler: starting\n");
    scheduler_run();
}

struct pcb *get_current(void) { return current; }

void proc_exit_current(void) {
    if (current == 0 || current->state != PROC_RUNNING)
        return;

    struct pcb *p = current;
    p->state = PROC_ZOMBIE;
    swtch(&p->context, &sched_context);
    panic("exited process resumed");
}

pgtable_t create_user_pgtable(void) {
    pgtable_t pgtable;
    pgtable = (pgtable_t) page_alloc();

    if(pgtable == 0) {
        panic("Physical memory allocation failed while creating user pgtable!");
    }

    // format all to zeros initially
    memset(pgtable, 0, PAGE_SIZE);

    // map kernel from the kernel page table to upper half
    for (int i = 256; i < 512; i++) {
        pgtable[i] = kernel_pgtable[i];
    }

    return pgtable;
}

void map_code(pgtable_t pgtable, char *data, unsigned long size) {
    unsigned long vaddr = 0;

    for(unsigned long offset = 0; offset < size; offset += PAGE_SIZE) {
        void *paddr = page_alloc();
        if(paddr == 0) {
            panic("Physical memory allocation failed while mapping code for process!");
        }
        unsigned long bytes = size - offset;
        if(bytes > PAGE_SIZE) bytes = PAGE_SIZE;
        memset(paddr, 0, PAGE_SIZE);
        memmove(paddr, data + offset, bytes);
        vmem_map(pgtable, vaddr+offset, virt_to_phys((unsigned long)paddr), PAGE_SIZE, PTE_U | PTE_R | PTE_W | PTE_X);
    }

}

unsigned long map_stack(pgtable_t pgtable) {
    void *phyaddr = page_alloc();
    unsigned long vaddr = USER_STACK_BASE - PAGE_SIZE;

    if(phyaddr == 0) return 0;

    vmem_map(pgtable, vaddr, virt_to_phys((unsigned long)phyaddr), PAGE_SIZE, PTE_U | PTE_R | PTE_W);
    return USER_STACK_BASE;
}
