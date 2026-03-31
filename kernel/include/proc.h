#pragma once
#include <stdint.h>
#include <vmem.h>

#define KSTACK_SIZE 8192
#define MAX_PROCS   4
#define USER_STACK_BASE 0x40000000UL

struct context {
    uint64_t ra;    // offset 0
    uint64_t sp;    // offset 8
    uint64_t s0;    // offset 16
    uint64_t s1;    // offset 24
    uint64_t s2;    // offset 32
    uint64_t s3;    // offset 40
    uint64_t s4;    // offset 48
    uint64_t s5;    // offset 56
    uint64_t s6;    // offset 64
    uint64_t s7;    // offset 72
    uint64_t s8;    // offset 80
    uint64_t s9;    // offset 88
    uint64_t s10;   // offset 96
    uint64_t s11;   // offset 104
};

typedef enum {
    PROC_UNUSED  = 0,
    PROC_READY   = 1,
    PROC_RUNNING = 2,
} proc_state_t;

struct pcb {
    int            pid;
    proc_state_t   state;
    void         (*entry)(void);
    struct context context;
    uint8_t        kstack[KSTACK_SIZE];
};

void sched_init(void);
void yield(void);
void swtch(struct context *old, struct context *new);

pgtable_t create_user_pgtable(void);
void map_code(pgtable_t pgtable, char *data, unsigned long size);
unsigned long map_stack(pgtable_t pgtable);