#include <timer.h>
#include <proc.h>

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
    for (struct pcb *p = proc_list_head(); p; p = p->next) {
        if (p->state == PROC_SLEEPING && p->wake_tick != 0 &&
            p->wake_tick <= ticks) {
            p->state    = PROC_READY;
            p->wake_tick = 0;
        }
    }

    yield();   // preempt the current thread on each tick
}