// SBUnix HAL declarations.
#pragma once

#include <stdint.h>
#include "py/mpconfig.h"

static inline mp_uint_t mp_hal_ticks_ms(void) {
    return 0;
}

static inline mp_uint_t mp_hal_ticks_us(void) {
    return 0;
}

static inline mp_uint_t mp_hal_ticks_cpu(void) {
    return 0;
}

static inline void mp_hal_set_interrupt_char(char c) {
    (void)c;
}

void mp_hal_delay_ms(mp_uint_t ms);
void mp_hal_delay_us(mp_uint_t us);
