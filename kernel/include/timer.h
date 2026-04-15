#ifndef _TIMER_H
#define _TIMER_H

#include <stdint.h>
#include <sbi.h>
#include <riscv.h>
#include <printk.h>

// QEMU virt machine: mtime frequency = 10 MHz.
// 100 ticks/sec → 10 ms per tick.
#define TICKS_PER_SEC  100UL
#define TIMER_INTERVAL (10000000UL / TICKS_PER_SEC)

static inline uint64_t ms_to_ticks(uint64_t ms) {
    return (ms * TICKS_PER_SEC + 999UL) / 1000UL;  // round up
}

void     timer_init(void);
void     timer_handler(void);
uint64_t timer_ticks(void);   // current tick count

#endif
