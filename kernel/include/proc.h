#include<drivers/uart.h>
#include<pmem.h>
#include<printk.h>
#include<string.h>
#include<vmem.h>

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct proc {
    int pid;
    char name[16];
    enum procstate state;
    int killed;
    int xstate;

    struct proc *parent;

    unsigned long kstack;
    unsigned long memsize;
    pgtable_t pgtable;

};

#define NUMPROC 128

#define USER_STACK_BASE 0x40000000

#define KSTACK_BASE (KVMEM_OFFSET - PAGE_SIZE)
#define KSTACK(i) (KSTACK_BASE - ((unsigned long)(i) * 2 * PAGE_SIZE))