#include <proc.h>
#include <pmem.h>
#include <printk.h>
#include <riscv.h>
#include <string.h>

static struct pcb     procs[MAX_PROCS];
static int            current_idx = -1;
static struct context sched_context;

// ----------------------------------------------------------------
// forkret — first-run entry point for new threads.
//
// When swtch first transfers control to a new thread, there is no
// prior sret to restore interrupts (SIE is cleared because we
// inherited the scheduler's interrupt-disabled state).  forkret
// enables interrupts and calls the thread's entry function.
// ----------------------------------------------------------------

static void forkret(void) {
    struct pcb *p = &procs[current_idx];
    write_sstatus(read_sstatus() | SSTATUS_SIE);
    p->entry();

    // If the thread function ever returns, mark it unused and
    // switch back to the scheduler permanently.
    p->state = PROC_UNUSED;
    swtch(&p->context, &sched_context);
}

// ----------------------------------------------------------------
// Test threads
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
// Initialize a PCB slot
// ----------------------------------------------------------------

static void proc_init(int idx, void (*func)(void)) {
    struct pcb *p = &procs[idx];

    p->pid   = idx + 1;
    p->state = PROC_READY;
    p->entry = func;

    // zero out context
    uint8_t *ctx = (uint8_t *)&p->context;
    for (int i = 0; i < (int)sizeof(struct context); i++)
        ctx[i] = 0;

    // sp = top of this thread's kernel stack (stack grows down)
    p->context.sp = (uint64_t)(p->kstack + KSTACK_SIZE);

    // ra = forkret, so the first swtch into this thread enables
    // interrupts and calls the real entry function.
    p->context.ra = (uint64_t)forkret;
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
    struct pcb *p = &procs[current_idx];

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
        int found = -1;
        int start = (current_idx + 1) % MAX_PROCS;

        for (int i = 0; i < MAX_PROCS; i++) {
            int idx = (start + i) % MAX_PROCS;
            if (procs[idx].state == PROC_READY) {
                found = idx;
                break;
            }
        }

        if (found == -1)
            continue;

        current_idx = found;
        procs[found].state = PROC_RUNNING;
        swtch(&sched_context, &procs[found].context);
    }
}

// ----------------------------------------------------------------
// sched_init — called once from boot(), never returns
// ----------------------------------------------------------------

void sched_init(void) {
    for (int i = 0; i < MAX_PROCS; i++)
        procs[i].state = PROC_UNUSED;

    proc_init(0, thread_a);
    proc_init(1, thread_b);

    printk("scheduler: starting\n");
    scheduler_run();
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
        vmem_map(pgtable, vaddr+offset, (unsigned long)paddr, PAGE_SIZE, PTE_U | PTE_R | PTE_W | PTE_X);
    }

}

unsigned long map_stack(pgtable_t pgtable) {
    void *phyaddr = page_alloc();
    unsigned long vaddr = USER_STACK_BASE - PAGE_SIZE;

    if(phyaddr == 0) return 0;

    vmem_map(pgtable, vaddr, (unsigned long)phyaddr, PAGE_SIZE, PTE_U | PTE_R | PTE_W);
    return USER_STACK_BASE;
}
