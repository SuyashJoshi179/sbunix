#ifndef _RISCV_H
#define _RISCV_H

#include <stdint.h>

#define SSTATUS_SIE (1 << 1)
#define SSTATUS_SPIE (1 << 5)
#define SIE_STIE (1 << 5)

static inline uint64_t read_sstatus(void) {
    uint64_t sstatus;
    asm volatile("csrr %0, sstatus" : "=r" (sstatus));
    return sstatus;
}

static inline void write_sstatus(uint64_t sstatus) {
    asm volatile("csrw sstatus, %0" : : "r" (sstatus));
}

static inline uint64_t read_sie(void) {
    uint64_t sie;
    asm volatile("csrr %0, sie" : "=r" (sie));
    return sie;
}

static inline void write_sie(uint64_t sie) {
    asm volatile("csrw sie, %0" : : "r" (sie));
}

static inline uint64_t read_stvec(void) {
    uint64_t stvec;
    asm volatile("csrr %0, stvec" : "=r" (stvec));
    return stvec;
}

static inline void write_stvec(uint64_t stvec) {
    asm volatile("csrw stvec, %0" : : "r" (stvec));
}

static inline uint64_t read_scause(void) {
    uint64_t scause;
    asm volatile("csrr %0, scause" : "=r" (scause));
    return scause;
}

static inline uint64_t read_sepc(void) {
    uint64_t sepc;
    asm volatile("csrr %0, sepc" : "=r" (sepc));
    return sepc;
}

static inline void read_stval(uint64_t *stval) {
    asm volatile("csrr %0, stval" : "=r" (*stval));
}

static inline uint64_t read_time(void) {
    uint64_t val;
    asm volatile("rdtime %0" : "=r" (val));
    return val;
}

static inline uint64_t read_sscratch(void) {
    uint64_t val;
    asm volatile("csrr %0, sscratch" : "=r"(val));
    return val;
}

static inline void write_sscratch(uint64_t val) {
    asm volatile("csrw sscratch, %0" : : "r"(val));
}
#endif