#ifndef _DRIVERS_RTC_H
#define _DRIVERS_RTC_H

#include <stdint.h>

// Goldfish RTC on QEMU virt: returns wall-clock nanoseconds since the
// Unix epoch (1970-01-01 UTC).
void     rtc_init(void);
uint64_t rtc_read_ns(void);

#endif
