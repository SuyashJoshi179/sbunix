#ifndef _SBI_H
#define _SBI_H

#include <stdint.h>

struct sbi_ret {
    long error;
    long value;
};

struct sbi_ret sbi_call(long ext, uint64_t fid, uint64_t arg0);
void sbi_set_timer(uint64_t stime_value);

#endif
