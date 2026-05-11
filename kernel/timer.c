#include <timer.h>
#include <pmem.h>
#include <proc.h>
#include <vmem.h>
#include <signal.h>

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
        /*
         * Guards against stale-PCB list-link dereference seen under
         * leak/resource churn stress (observed as kernel fetch faults when
         * this check is removed); see docs/handoff/phase9_5_impl_report.md.
         */
        unsigned long pva = (unsigned long)p;
        unsigned long ppa = virt_to_phys(pva);
        if ((pva & (PAGE_SIZE - 1)) || ppa < KERN_BASE || ppa >= PHYMEM_END) {
            printk("timer: bad pcb ptr p=%p prev=%p head=%p\n",
                   p, prev, proc_list_head());
            panic("timer: stale pcb pointer hypothesis guard");
        }
        if (p->state == PROC_SLEEPING && p->wake_tick != 0 &&
            p->wake_tick <= ticks) {
            p->state    = PROC_READY;
            p->wake_tick = 0;
        }
        /* Deliver SIGALRM when the per-process alarm deadline expires.
         * Skip zombies and unused slots so we don't fire on a recycled PCB. */
        if (p->alarm_tick != 0 && p->alarm_tick <= ticks &&
            p->state != PROC_UNUSED && p->state != PROC_ZOMBIE) {
            p->alarm_tick = 0;
            send_signal(p, SIGALRM);
        }
    }

    /* No yield here: preemption from in-kernel timer traps would violate
     * the "IRQ-off is the lock" model — kernel critical sections that
     * happened to be running with interrupts on could lose CPU to another
     * proc mid-update. The trap handler gates the preempt on SPP=0
     * (interrupted user mode) instead. */
}