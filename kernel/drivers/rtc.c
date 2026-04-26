// Goldfish RTC driver (QEMU virt machine).
//
// Memory-mapped at physical 0x101000 (per device tree):
//   reg 0x00 (TIME_LOW)  — RO, low 32 bits of nanoseconds since Unix epoch.
//                          Reading LOW latches the corresponding HIGH value.
//   reg 0x04 (TIME_HIGH) — RO, high 32 bits latched by the last TIME_LOW read.
//
// The kernel maps this 4 KiB MMIO page in vmem_init(); see kernel/vmem.c.

#include <drivers/rtc.h>
#include <printk.h>
#include <vmem.h>
#include <stdint.h>

#define GOLDFISH_RTC_PHYS  0x101000UL
#define GOLDFISH_RTC_VIRT  (KVMEM_OFFSET + GOLDFISH_RTC_PHYS)

#define RTC_TIME_LOW   0x00
#define RTC_TIME_HIGH  0x04

static inline volatile uint32_t *rtc_reg(unsigned long off) {
    return (volatile uint32_t *)(GOLDFISH_RTC_VIRT + off);
}

void rtc_init(void) {
    // Driver is stateless; touch the registers once to confirm the MMIO
    // mapping is live (a fault here surfaces a bad page-table setup early).
    uint64_t ns  = rtc_read_ns();
    uint64_t sec = ns / 1000000000UL;
    printk("rtc: goldfish epoch=%lu seconds since 1970-01-01 UTC\n", sec);
}

uint64_t rtc_read_ns(void) {
    // Order matters: LOW must be read first; HIGH is latched by that read.
    uint32_t lo = *rtc_reg(RTC_TIME_LOW);
    uint32_t hi = *rtc_reg(RTC_TIME_HIGH);
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}
