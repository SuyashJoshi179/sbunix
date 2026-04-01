#ifndef _TIMER_H
#define _TIMER_H

#include <stdint.h>
#include <sbi.h>
#include <riscv.h>
#include <printk.h>

void timer_init(void);
void timer_handler(void);

#endif
