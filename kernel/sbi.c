#include <sbi.h>

struct sbi_ret sbi_call(long ext, uint64_t fid, uint64_t arg0) {
    struct sbi_ret ret;
    register uint64_t a0 asm("a0") = arg0;
    register uint64_t a1 asm("a1");
    register uint64_t a6 asm("a6") = fid;
    register uint64_t a7 asm("a7") = ext;
    asm volatile("ecall"
                    : "+r" (a0), "=r" (a1)
                    : "r" (a6), "r" (a7)
                    : "memory");
    ret.error = a0;
    ret.value = a1;
    return ret;     
}

void sbi_set_timer(uint64_t stime_value) {
    sbi_call(0, 0, stime_value);
}