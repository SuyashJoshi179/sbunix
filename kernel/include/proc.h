#pragma once
#include <stdint.h>
#include <vmem.h>

#define KSTACK_SIZE PAGE_SIZE   // one 4KB page per process kernel stack

// Callee-saved register set used by swtch() for kernel-to-kernel switches.
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
    PROC_UNUSED   = 0,
    PROC_READY    = 1,
    PROC_RUNNING  = 2,
    PROC_SLEEPING = 3,
    PROC_ZOMBIE   = 4,
} proc_state_t;

struct pcb {
    int            pid;
    int            parent_pid;
    int            exit_status;
    proc_state_t   state;
    uint8_t        is_user;         // 1 for user processes, 0 for kernel threads

    void         (*entry)(void);    // entry function (kernel threads only)

    // User process fields (populated by exec/spawn; 0 for kernel threads)
    pgtable_t      pagetable;       // user page table root (kernel virtual addr)
    unsigned long  user_entry;      // ELF entry point
    unsigned long  user_sp;         // user stack pointer

    struct context context;         // saved registers for swtch()
    void          *kstack_page;     // kernel stack page (kernel virtual addr)

    struct pcb    *next;            // intrusive linked list
};

void  sched_init(void);
void  yield(void);
void  swtch(struct context *old, struct context *new);

// Returns the currently running PCB (NULL if scheduler is running).
struct pcb *current_proc(void);
