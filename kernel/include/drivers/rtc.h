#ifndef _DRIVERS_RTC_H
#define _DRIVERS_RTC_H

#include <stdint.h>

// Goldfish RTC on QEMU virt: returns wall-clock nanoseconds since the
// Unix epoch (1970-01-01 UTC).
void     rtc_init(void);
uint64_t rtc_read_ns(void);

// Wall-clock view that honours clock_settime adjustments. The Goldfish
// TIME_LOW/HIGH registers are read-only on QEMU, so clock_settime can't
// actually rewrite hardware — it stores a signed offset that this helper
// adds to the raw RTC reading. Use this (not rtc_read_ns) anywhere the
// reading is meant to satisfy CLOCK_REALTIME / gettimeofday semantics.
uint64_t realtime_ns(void);

// Set the wall-clock to `target_ns` nanoseconds since the Unix epoch by
// computing offset = target_ns - rtc_read_ns(). Subsequent realtime_ns()
// calls return target_ns plus any RTC advance since this call.
void     clock_set_realtime_ns(uint64_t target_ns);

#endif
