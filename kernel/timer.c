#include <timer.h>
#include <proc.h>

#define TIMER_INTERVAL 10000000

// lives in .bss section
static uint64_t ticks = 0;

void timer_init(void) {
    uint64_t now = read_time();
    sbi_set_timer(now + TIMER_INTERVAL);
}

void timer_handler(void) {
    ticks++;
    uint64_t now = read_time();
    sbi_set_timer(now + TIMER_INTERVAL);
    yield();   // preempt the current thread on each tick
}