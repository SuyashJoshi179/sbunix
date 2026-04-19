#include <timer.h>
#include <pmem.h>
#include <proc.h>
#include <vmem.h>

// lives in .bss section
static uint64_t ticks = 0;

uint64_t timer_ticks(void) { return ticks; }

void timer_init(void) {
    uint64_t now = read_time();
    sbi_set_timer(now + TIMER_INTERVAL);
}

void timer_handler(void) {
    ticks++;
    sbi_set_timer(read_time() + TIMER_INTERVAL);

    // Wake any processes sleeping on a timed deadline.
    struct pcb *prev = 0;
    for (struct pcb *p = proc_list_head(); p; prev = p, p = p->next) {
        /* Invariant: scheduler process-list links must be aligned RAM pointers. */
        unsigned long pva = (unsigned long)p;
        unsigned long ppa = virt_to_phys(pva);
        if ((pva & (PAGE_SIZE - 1)) || ppa < KERN_BASE || ppa >= PHYMEM_END) {
            printk("timer: bad pcb ptr p=%p prev=%p head=%p\n",
                   p, prev, proc_list_head());
            panic("timer: corrupted process list");
        }
        if (p->state == PROC_SLEEPING && p->wake_tick != 0 &&
            p->wake_tick <= ticks) {
            p->state    = PROC_READY;
            p->wake_tick = 0;
        }
    }

    yield();   // preempt the current thread on each tick
}