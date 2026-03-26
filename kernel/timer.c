#include <timer.h>

#define TIMER_INTERVAL 10000000

// lives in .bss section
static uint64_t ticks = 0;

void timer_init(void) {
    uint64_t now = read_time();
    sbi_set_timer(now + TIMER_INTERVAL);
}

void timer_handler(void) {
    ticks++;
    printk("Timer interrupt: ticks=%ld\n", ticks);
    uint64_t now = read_time();
    sbi_set_timer(now + TIMER_INTERVAL);
}